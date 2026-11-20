#include "PresetManager.h"

#include "EurekaFactory.h"

namespace ambika_plugin
{
    namespace
    {
        constexpr const char* kFormat = "eureka-preset";
        constexpr int kFormatVersion = 1;
        const juce::Identifier kStateNode { "Presets" };
        const juce::Identifier kPartNode { "Part" };

        // Documents/eureka/presets on every platform: easy to find, and
        // writable from sandboxed hosts that cannot write to ~/Library.
        juce::File defaultDirectory()
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile ("eureka").getChildFile ("presets");
        }

        juce::var parseFile (const juce::File& file)
        {
            juce::var json;
            if (juce::JSON::parse (file.loadFileAsString(), json).failed())
                return {};
            if (json.getProperty ("format", {}).toString() != kFormat)
                return {};
            return json;
        }

        juce::StringArray toStringArray (const juce::var& value)
        {
            juce::StringArray result;
            if (auto* array = value.getArray())
                for (const auto& item : *array)
                    if (item.toString().trim().isNotEmpty())
                        result.addIfNotAlreadyThere (item.toString().trim());
            return result;
        }

        juce::var toVar (const juce::StringArray& strings)
        {
            juce::Array<juce::var> array;
            for (const auto& s : strings)
                array.add (s);
            return array;
        }

        std::vector<int> defaultValues()
        {
            std::vector<int> values;
            for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
                values.push_back (ambika::plugin::GetParameter (i).default_value);
            return values;
        }
    }

    const juce::StringArray& PresetManager::standardTags()
    {
        static const juce::StringArray tags { "Bass", "Lead", "Pad", "Keys", "Pluck", "Brass", "Strings",
                                              "Vocal", "Bell", "Drum", "FX", "Texture", "Mono", "Poly" };
        return tags;
    }

    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s)
        : state (s), directory (defaultDirectory())
    {
        // No file system access here: hosts construct the plug-in while
        // scanning, and touching ~/Documents from a scanner process triggers a
        // macOS privacy check it cannot answer (Ableton then marks the plug-in
        // as failed). The folder is created when presets are first used.
        for (auto& c : current)
            c.snapshot = defaultValues();
    }

    void PresetManager::setDirectory (const juce::File& newDirectory)
    {
        directory = newDirectory;
    }

    juce::String PresetManager::sanitiseFileName (const juce::String& name)
    {
        auto legal = juce::File::createLegalFileName (name.trim());
        return legal.isEmpty() ? juce::String ("Untitled") : legal;
    }

    juce::File PresetManager::fileForName (const juce::String& name) const
    {
        return directory.getChildFile (sanitiseFileName (name) + fileExtension);
    }

    bool PresetManager::readInfo (const juce::File& file, PresetInfo& info)
    {
        const auto json = parseFile (file);
        if (json.isVoid())
            return false;
        info.file = file;
        info.name = json.getProperty ("name", file.getFileNameWithoutExtension()).toString();
        info.tags = toStringArray (json.getProperty ("tags", {}));
        info.author = json.getProperty ("author", {}).toString();
        info.comment = json.getProperty ("comment", {}).toString();
        info.modified = file.getLastModificationTime();
        return true;
    }

    std::vector<PresetInfo> PresetManager::scan() const
    {
        // Called when the browser opens or the user steps through presets.
        directory.createDirectory();
        std::vector<PresetInfo> presets (factoryPresets());
        if (! directory.isDirectory())
            return presets;
        for (const auto& entry : juce::RangedDirectoryIterator (directory, true, juce::String ("*") + fileExtension,
                                                                juce::File::findFiles))
        {
            PresetInfo info;
            if (readInfo (entry.getFile(), info))
                presets.push_back (std::move (info));
        }
        std::sort (presets.begin(), presets.end(), [] (const PresetInfo& a, const PresetInfo& b)
                   { return a.name.compareNatural (b.name) < 0; });
        return presets;
    }

    std::vector<int> PresetManager::captureValues (int part) const
    {
        std::vector<int> values;
        for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
        {
            const auto& info = ambika::plugin::GetParameter (i);
            auto* raw = state.getRawParameterValue (partParameterId (part, info));
            values.push_back (toEngineValue (info, raw->load()));
        }
        return values;
    }

    void PresetManager::applyValues (int part, const std::vector<int>& values)
    {
        for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
        {
            const auto& info = ambika::plugin::GetParameter (i);
            auto* parameter = state.getParameter (partParameterId (part, info));
            const int value = juce::jlimit<int> (info.min_value, info.max_value, values[(size_t) i]);
            const float denormalised = (float) (info.choices != nullptr ? value - info.min_value : value);
            const float normalised = parameter->convertTo0to1 (denormalised);
            if (! juce::exactlyEqual (parameter->getValue(), normalised))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (normalised);
                parameter->endChangeGesture();
            }
        }
    }

    juce::Result PresetManager::load (int part, const juce::File& file)
    {
        if (isFactory (file) || ! isEurekaPreset (file))
        {
            ambika_format::Bundle bundle;
            juce::Result parsed = juce::Result::ok();
            if (isFactory (file))
            {
                int size = 0;
                const char* bytes = nullptr;
                for (int i = 0; i < EurekaFactory::namedResourceListSize && bytes == nullptr; ++i)
                    if (file.getFileName() == EurekaFactory::originalFilenames[i])
                        bytes = EurekaFactory::getNamedResource (EurekaFactory::namedResourceList[i], size);
                if (bytes == nullptr)
                    return juce::Result::fail ("Unknown factory program " + file.getFileName());
                parsed = ambika_format::parse (juce::MemoryBlock (bytes, (size_t) size), file.getFileNameWithoutExtension(), bundle);
            }
            else
                parsed = ambika_format::parseFile (file, bundle);
            if (parsed.failed())
                return juce::Result::fail ("\"" + file.getFileName() + "\": " + parsed.getErrorMessage());
            return loadAmbika (part, bundle, file);
        }
        const auto json = parseFile (file);
        if (json.isVoid())
            return juce::Result::fail ("\"" + file.getFileName() + "\" is not an eureka! preset.");

        auto values = defaultValues();
        const auto parameters = json.getProperty ("parameters", {});
        for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
        {
            const auto& info = ambika::plugin::GetParameter (i);
            if (parameters.hasProperty (info.id))
                values[(size_t) i] = (int) parameters.getProperty (info.id, info.default_value);
        }
        applyValues (part, values);

        auto& c = current[(size_t) part];
        PresetInfo info;
        readInfo (file, info);
        c.name = info.name;
        c.file = file;
        c.tags = info.tags;
        c.author = info.author;
        c.comment = info.comment;
        c.snapshot = captureValues (part);
        return juce::Result::ok();
    }

    juce::Result PresetManager::save (int part, const juce::String& name, const juce::StringArray& tags,
                                      const juce::String& author, const juce::String& comment, bool overwrite)
    {
        if (name.trim().isEmpty())
            return juce::Result::fail ("Please give the preset a name.");
        const auto file = fileForName (name);
        if (file.existsAsFile() && ! overwrite)
            return juce::Result::fail ("A preset called \"" + name.trim() + "\" already exists.");
        if (const auto created = directory.createDirectory(); created.failed())
            return juce::Result::fail ("Could not create the preset folder " + directory.getFullPathName()
                                       + ": " + created.getErrorMessage());

        const auto values = captureValues (part);
        if (const auto written = writePresetFile (file, name, tags, author, comment, values); written.failed())
            return written;

        auto& c = current[(size_t) part];
        c.name = name.trim();
        c.file = file;
        c.tags = tags;
        c.author = author.trim();
        c.comment = comment.trim();
        c.snapshot = values;
        return juce::Result::ok();
    }

    void PresetManager::initialise (int part)
    {
        const auto values = defaultValues();
        applyValues (part, values);
        current[(size_t) part] = Current();
        current[(size_t) part].snapshot = captureValues (part);
    }

    void PresetManager::initialiseAll()
    {
        for (int part = 0; part < kNumParts; ++part)
            initialise (part);

        juce::StringArray routing { kVoiceModeId, kUnisonId };
        for (int voice = 0; voice < kNumVoices; ++voice)
            routing.add (voicePartId (voice));
        for (int part = 0; part < kNumParts; ++part)
        {
            routing.add (partKeyLowId (part));
            routing.add (partKeyHighId (part));
        }
        for (const auto& id : routing)
        {
            if (auto* parameter = state.getParameter (id))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        }
    }

    juce::Result PresetManager::writePresetFile (const juce::File& file, const juce::String& name,
                                                 const juce::StringArray& tags, const juce::String& author,
                                                 const juce::String& comment, const std::vector<int>& values) const
    {
        auto* parameters = new juce::DynamicObject();
        for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
            parameters->setProperty (ambika::plugin::GetParameter (i).id, values[(size_t) i]);

        auto* root = new juce::DynamicObject();
        root->setProperty ("format", kFormat);
        root->setProperty ("version", kFormatVersion);
        root->setProperty ("name", name.trim());
        root->setProperty ("tags", toVar (tags));
        root->setProperty ("author", author.trim());
        root->setProperty ("comment", comment.trim());
        root->setProperty ("parameters", juce::var (parameters));

        if (! file.replaceWithText (juce::JSON::toString (juce::var (root), false)))
            return juce::Result::fail ("Could not write " + file.getFullPathName()
                                       + (file.getParentDirectory().hasWriteAccess()
                                              ? juce::String()
                                              : juce::String (" (no write permission for this folder)")));
        return juce::Result::ok();
    }

    //==============================================================================
    namespace
    {
        const juce::String kFactoryRoot { "/eureka-factory/" };
    }

    bool PresetManager::isFactory (const juce::File& file)
    {
        // On Windows juce::File makes "/eureka-factory/x" into "C:\eureka-factory\x":
        // compare with the drive letter removed and forward slashes.
        auto path = file.getFullPathName().replaceCharacter ('\\', '/');
        if (path.length() > 1 && path[1] == ':')
            path = path.substring (2);
        return path.startsWith (kFactoryRoot);
    }

    bool PresetManager::isEurekaPreset (const juce::File& file)
    {
        return file.hasFileExtension (fileExtension);
    }

    const std::vector<PresetInfo>& PresetManager::factoryPresets()
    {
        static const std::vector<PresetInfo> presets = []
        {
            std::vector<PresetInfo> list;
            for (int i = 0; i < EurekaFactory::namedResourceListSize; ++i)
            {
                int size = 0;
                const auto* bytes = EurekaFactory::getNamedResource (EurekaFactory::namedResourceList[i], size);
                ambika_format::Bundle bundle;
                const juce::String fileName (EurekaFactory::originalFilenames[i]);
                if (bytes == nullptr
                    || ambika_format::parse (juce::MemoryBlock (bytes, (size_t) size), fileName, bundle).failed())
                    continue;
                PresetInfo info;
                info.file = juce::File (kFactoryRoot + fileName);
                info.name = bundle.name;
                info.tags = { "Factory" };
                info.author = "Mutable Instruments";
                info.comment = "Ambika factory program " + fileName.upToFirstOccurrenceOf (".", false, false) + ".";
                info.factory = true;
                list.push_back (info);
            }
            return list;
        }();
        return presets;
    }

    void PresetManager::setParameter (const juce::String& id, float value)
    {
        if (auto* parameter = state.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    }

    juce::Result PresetManager::loadAmbika (int part, const ambika_format::Bundle& bundle, const juce::File& source)
    {
        using namespace ambika_format;
        const bool factory = isFactory (source);
        auto remember = [&] (int p, const juce::String& name)
        {
            auto& c = current[(size_t) p];
            c.name = name;
            c.file = factory ? source : juce::File();  // Imported files are not saved over.
            c.tags = factory ? juce::StringArray { "Factory" } : juce::StringArray { "Ambika" };
            c.author = factory ? "Mutable Instruments" : juce::String();
            c.comment = {};
            c.snapshot = captureValues (p);
        };

        if (bundle.isMulti())
        {
            // A multi: each part's patch and settings, then the routing.
            for (int p = 0; p < kNumParts; ++p)
            {
                const auto* patch = bundle.find (kPatch, p + 1);
                const auto* settings = bundle.find (kPart, p + 1);
                if (patch == nullptr)
                    continue;
                applyValues (p, toEngineValues (patch->data.data(), settings != nullptr ? settings->data.data() : nullptr));
                remember (p, bundle.name + " " + juce::String (p + 1));
            }
            if (const auto* multi = bundle.find (kMulti, 0))
            {
                // PartMapping x6: midi channel, key low, key high, voice
                // allocation bits. A voice belongs to the first part
                // claiming it, as on the hardware.
                std::array<int, kNumVoices> owner;
                owner.fill (0);
                for (int p = kNumParts - 1; p >= 0; --p)
                {
                    const auto* mapping = multi->data.data() + p * 4;
                    setParameter (partKeyLowId (p), (float) juce::jlimit (0, 127, (int) mapping[1]));
                    setParameter (partKeyHighId (p), (float) juce::jlimit (0, 127, (int) mapping[2]));
                    for (int v = 0; v < kNumVoices; ++v)
                        if (mapping[3] & (1 << v))
                            owner[(size_t) v] = p + 1;
                }
                for (int v = 0; v < kNumVoices; ++v)
                    setParameter (voicePartId (v), (float) owner[(size_t) v]);
                setParameter (kVoiceModeId, 0.0f);  // Per part, like the hardware.
                setParameter (kUnisonId, 1.0f);
            }
            return juce::Result::ok();
        }

        // A patch or program for one part: prefer objects addressed to the
        // current part, else the first of each kind.
        const Object* patch = bundle.find (kPatch, 0);
        const Object* settings = bundle.find (kPart, 0);
        for (const auto& o : bundle.objects)
        {
            if (patch == nullptr && o.type == kPatch) patch = &o;
            if (settings == nullptr && o.type == kPart) settings = &o;
        }
        if (patch == nullptr)
            return juce::Result::fail ("\"" + bundle.name + "\" has no patch (eureka! has no sequencer to load "
                                       "sequences into).");
        applyValues (part, toEngineValues (patch->data.data(), settings != nullptr ? settings->data.data() : nullptr));
        remember (part, bundle.name);
        return juce::Result::ok();
    }

    int PresetManager::importToLibrary (const juce::Array<juce::File>& files, juce::StringArray& errors)
    {
        using namespace ambika_format;
        const auto folder = directory.getChildFile ("Imported");
        if (const auto created = folder.createDirectory(); created.failed())
        {
            errors.add ("Could not create " + folder.getFullPathName());
            return 0;
        }
        int written = 0;
        auto add = [&] (const juce::String& name, const Object* patch, const Object* settings, const juce::String& source)
        {
            auto file = folder.getChildFile (sanitiseFileName (name) + fileExtension);
            for (int n = 2; file.existsAsFile(); ++n)
                file = folder.getChildFile (sanitiseFileName (name) + " " + juce::String (n) + fileExtension);
            const auto values = toEngineValues (patch->data.data(), settings != nullptr ? settings->data.data() : nullptr);
            const auto result = writePresetFile (file, name, { "Ambika" }, {}, "Imported from " + source, values);
            if (result.wasOk())
                ++written;
            else
                errors.add (result.getErrorMessage());
        };

        for (const auto& item : files)
        {
            juce::Array<juce::File> inputs;
            if (item.isDirectory())
                for (const auto& entry : juce::RangedDirectoryIterator (item, true, kFilePatterns, juce::File::findFiles))
                    inputs.add (entry.getFile());
            else
                inputs.add (item);

            for (const auto& file : inputs)
            {
                Bundle bundle;
                if (const auto parsed = parseFile (file, bundle); parsed.failed())
                {
                    errors.add (file.getFileName() + ": " + parsed.getErrorMessage());
                    continue;
                }
                if (bundle.isMulti())
                {
                    for (int p = 0; p < kNumParts; ++p)
                        if (const auto* patch = bundle.find (kPatch, p + 1))
                            add (bundle.name + " part " + juce::String (p + 1), patch, bundle.find (kPart, p + 1),
                                 file.getFileName());
                    continue;
                }
                const Object* patch = nullptr;
                const Object* settings = nullptr;
                for (const auto& o : bundle.objects)
                {
                    if (patch == nullptr && o.type == kPatch) patch = &o;
                    if (settings == nullptr && o.type == kPart) settings = &o;
                }
                if (patch == nullptr)
                    errors.add (file.getFileName() + ": no patch (sequences are not imported)");
                else
                    add (bundle.name, patch, settings, file.getFileName());
            }
        }
        return written;
    }

    juce::Result PresetManager::rename (const juce::File& file, const juce::String& newName)
    {
        if (isFactory (file))
            return juce::Result::fail ("Factory presets cannot be renamed. Load it and use Save as...");
        if (newName.trim().isEmpty())
            return juce::Result::fail ("Please give the preset a name.");
        const auto json = parseFile (file);
        if (json.isVoid())
            return juce::Result::fail ("\"" + file.getFileName() + "\" is not an eureka! preset.");
        const auto target = file.getParentDirectory().getChildFile (sanitiseFileName (newName) + fileExtension);
        if (target != file && target.existsAsFile())
            return juce::Result::fail ("A preset called \"" + newName.trim() + "\" already exists.");

        if (auto* object = json.getDynamicObject())
            object->setProperty ("name", newName.trim());
        if (! target.replaceWithText (juce::JSON::toString (json, false)))
            return juce::Result::fail ("Could not write " + target.getFullPathName());
        if (target != file)
            file.deleteFile();

        for (auto& c : current)
            if (c.file == file)
            {
                c.file = target;
                c.name = newName.trim();
            }
        return juce::Result::ok();
    }

    juce::Result PresetManager::remove (const juce::File& file)
    {
        if (isFactory (file))
            return juce::Result::fail ("Factory presets cannot be deleted.");
        if (! file.existsAsFile())
            return juce::Result::fail ("The preset file no longer exists.");
        if (! (useTrash && file.moveToTrash()) && ! file.deleteFile())
            return juce::Result::fail ("Could not delete " + file.getFullPathName());
        for (auto& c : current)
            if (c.file == file)
                c.file = juce::File();
        return juce::Result::ok();
    }

    bool PresetManager::isModified (int part) const
    {
        return captureValues (part) != current[(size_t) part].snapshot;
    }

    juce::File PresetManager::neighbour (int part, int direction) const
    {
        const auto presets = scan();
        if (presets.empty())
            return {};
        const auto& file = current[(size_t) part].file;
        int index = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].file == file)
                index = (int) i;
        const int count = (int) presets.size();
        if (index < 0)
            return presets[direction > 0 ? 0 : (size_t) (count - 1)].file;
        return presets[(size_t) ((index + direction + count) % count)].file;
    }

    void PresetManager::writeState (juce::ValueTree& tree) const
    {
        tree.removeChild (tree.getChildWithName (kStateNode), nullptr);
        juce::ValueTree presets (kStateNode);
        for (int part = 0; part < kNumParts; ++part)
        {
            const auto& c = current[(size_t) part];
            juce::ValueTree node (kPartNode);
            node.setProperty ("index", part, nullptr);
            node.setProperty ("name", c.name, nullptr);
            node.setProperty ("file", c.file.getFullPathName(), nullptr);
            node.setProperty ("tags", c.tags.joinIntoString (","), nullptr);
            node.setProperty ("author", c.author, nullptr);
            node.setProperty ("comment", c.comment, nullptr);
            presets.appendChild (node, nullptr);
        }
        tree.appendChild (presets, nullptr);
    }

    void PresetManager::readState (const juce::ValueTree& tree)
    {
        for (int part = 0; part < kNumParts; ++part)
            current[(size_t) part] = Current();
        const auto presets = tree.getChildWithName (kStateNode);
        for (const auto& node : presets)
        {
            const int part = node.getProperty ("index", -1);
            if (part < 0 || part >= kNumParts)
                continue;
            auto& c = current[(size_t) part];
            c.name = node.getProperty ("name", "Init").toString();
            const auto path = node.getProperty ("file", {}).toString();
            c.file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
            c.tags = juce::StringArray::fromTokens (node.getProperty ("tags", {}).toString(), ",", {});
            c.tags.removeEmptyStrings();
            c.author = node.getProperty ("author", {}).toString();
            c.comment = node.getProperty ("comment", {}).toString();
        }
        // The project holds the sound as it was saved; that is the reference
        // for "modified" from now on.
        for (int part = 0; part < kNumParts; ++part)
            current[(size_t) part].snapshot = captureValues (part);
    }
}
