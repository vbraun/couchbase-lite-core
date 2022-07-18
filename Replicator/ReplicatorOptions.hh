//
//  ReplicatorOptions.hh
//
//  Copyright 2019-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#pragma once
#include "c4ReplicatorTypes.h"
#include "c4Database.hh"
#include "ReplicatorTypes.hh"
#include "fleece/RefCounted.hh"
#include "fleece/Fleece.hh"
#include "fleece/Expert.hh"  // for AllocedDict
#include <unordered_map>

namespace litecore { namespace repl {

    /** Replication configuration options */
    class Options final : public fleece::RefCounted {
    public:
        
        //---- Public fields:

        using Mode = C4ReplicatorMode;
        using Validator = C4ReplicatorValidationFunction;
        using PropertyEncryptor = C4ReplicatorPropertyEncryptionCallback;
        using PropertyDecryptor = C4ReplicatorPropertyDecryptionCallback;

        fleece::AllocedDict     properties;
        Validator               pushFilter              {nullptr};
        Validator               pullValidator           {nullptr};
        PropertyEncryptor       propertyEncryptor       {nullptr};
        PropertyDecryptor       propertyDecryptor       {nullptr};
        void*                   callbackContext         {nullptr};
        std::atomic<C4ReplicatorProgressLevel> progressLevel {kC4ReplProgressOverall};

        bool collectionAware() const {return _collectionAware;}
        bool isActive()        const {return _isActive;}
        const std::unordered_map<C4CollectionSpec, size_t>& collectionSpecToIndex() const {
            return _collectionSpecToIndex;
        }
        
        //---- Constructors/factories:

        Options(Mode push_, Mode pull_)
        {
            setCollectionOptions(push_, pull_);
            constructorCheck();
        }

        template <class SLICE>
        Options(Mode push_, Mode pull_, SLICE propertiesFleece)
        :properties(propertiesFleece)
        {
            setCollectionOptions(push_, pull_);
            constructorCheck();
        }

        explicit Options(C4ReplicatorParameters params)
        :properties(params.optionsDictFleece)
        ,pushFilter(params.pushFilter)
        ,pullValidator(params.validationFunc)
        ,propertyEncryptor(params.propertyEncryptor)
        ,propertyDecryptor(params.propertyDecryptor)
        ,callbackContext(params.callbackContext)
        {
            setCollectionOptions(params);
            constructorCheck();
        }

        Options(const Options &opt)     // copy ctor, required because std::atomic doesn't have one
        :pushFilter(opt.pushFilter)
        ,pullValidator(opt.pullValidator)
        ,propertyEncryptor(opt.propertyEncryptor)
        ,propertyDecryptor(opt.propertyDecryptor)
        ,callbackContext(opt.callbackContext)
        ,properties(slice(opt.properties.data())) // copy data, bc dtor wipes it
        ,progressLevel(opt.progressLevel.load())
        {
            setCollectionOptions(opt);
            constructorCheck();
        }

        static Options pushing(Mode mode =kC4OneShot)  {return Options(mode, kC4Disabled);}
        static Options pulling(Mode mode =kC4OneShot)  {return Options(kC4Disabled, mode);}
        static Options pushpull(Mode mode =kC4OneShot) {return Options(mode, mode);}
        static Options passive()                       {return Options(kC4Passive,kC4Passive);}

        //---- Property accessors:

        bool setProgressLevel(C4ReplicatorProgressLevel level) {
            return progressLevel.exchange(level) != level;
        }

        fleece::Array channels() const {return arrayProperty(kC4ReplicatorOptionChannels);}
        fleece::Array docIDs() const   {return arrayProperty(kC4ReplicatorOptionDocIDs);}
        fleece::slice filter() const  {return properties[kC4ReplicatorOptionFilter].asString();}
        fleece::Dict filterParams() const
                                  {return properties[kC4ReplicatorOptionFilterParams].asDict();}
        bool skipDeleted() const  {return boolProperty(kC4ReplicatorOptionSkipDeleted);}
        bool noIncomingConflicts() const  {return boolProperty(kC4ReplicatorOptionNoIncomingConflicts);}
        bool noOutgoingConflicts() const  {return boolProperty(kC4ReplicatorOptionNoIncomingConflicts);}

        bool disableDeltaSupport() const {return boolProperty(kC4ReplicatorOptionDisableDeltas);}
        bool disablePropertyDecryption() const {return boolProperty(kC4ReplicatorOptionDisablePropertyDecryption);}

        bool enableAutoPurge() const {
            if (!properties[kC4ReplicatorOptionAutoPurge])
                return true;
            return boolProperty(kC4ReplicatorOptionAutoPurge);
        }

        /** Returns a string that uniquely identifies the remote database; by default its URL,
            or the 'remoteUniqueID' option if that's present (for P2P dbs without stable URLs.) */
        fleece::slice remoteDBIDString(fleece::slice remoteURL) const {
            auto uniqueID = properties[kC4ReplicatorOptionRemoteDBUniqueID].asString();
            return uniqueID ? uniqueID : remoteURL;
        }

        fleece::Array arrayProperty(const char * name) const {
            return properties[name].asArray();
        }
        fleece::Dict dictProperty(const char * name) const {
            return properties[name].asDict();
        }

        //---- Property setters (used only by tests)

        template <class T>
        static fleece::AllocedDict updateProperties(const fleece::AllocedDict& properties, fleece::slice name, T value) {
            fleece::Encoder enc;
            enc.beginDict();
            if (value) {
                enc.writeKey(name);
                enc << value;
            }
            for (fleece::Dict::iterator i(properties); i; ++i) {
                fleece::slice key = i.keyString();
                if (key != name) {
                    enc.writeKey(key);
                    enc.writeValue(i.value());
                }
            }
            enc.endDict();
            return fleece::AllocedDict(enc.finish());
        }

        /** Sets/clears the value of a property.
            Warning: This rewrites the backing store of the properties, invalidating any
            Fleece value pointers or slices previously accessed from it. */
        template <class T>
        Options& setProperty(fleece::slice name, T value) {
            properties = Options::updateProperties(properties, name, value);
            return *this;
        }

        Options& setNoIncomingConflicts() {
            return setProperty(kC4ReplicatorOptionNoIncomingConflicts, true);
        }

        Options& setNoDeltas() {
            return setProperty(kC4ReplicatorOptionDisableDeltas, true);
        }

        Options& setNoPropertyDecryption() {
            return setProperty(kC4ReplicatorOptionDisablePropertyDecryption, true);
        }

        bool boolProperty(slice property) const   {return properties[property].asBool();}

        explicit operator std::string() const;

        // Collection Options:

        // The BLIP message, getCollections, specifies that the body consist of an array of
        // collection paths, e.g. '[“scope/foo”,”bar”,”zzz/buzz”]'. So, we convert the
        // CollecttionSpec given in C4ReplicatorParamters to slash separated path.
        static alloc_slice collectionSpecToPath(C4CollectionSpec spec, bool omitDefaultScope=true) {
            bool addScope = true;
            if (FLSlice_Compare(spec.scope, kC4DefaultScopeID) == 0 && omitDefaultScope) {
                addScope = false;
            }
            size_t size = addScope ? spec.scope.size + 1 : 0;
            size += spec.name.size;
            alloc_slice ret(size);
            void* buf = const_cast<void*>(ret.buf);
            size_t nameOffset = 0;
            if (addScope) {
                slice(spec.scope).copyTo(buf);
                ((uint8_t*)buf)[spec.scope.size] = '.';
                nameOffset = spec.scope.size + 1;
            }
            slice(spec.name).copyTo((uint8_t*)buf + nameOffset);
            return ret;
        }

        static C4CollectionSpec collectionPathToSpec(slice path) {
            const uint8_t* slash = path.findByte((uint8_t)'.');
            slice scope = kC4DefaultScopeID;
            slice name;
            if (slash != nullptr) {
                scope = slice {path.buf, static_cast<size_t>(slash - static_cast<const uint8_t*>(path.buf))};
                name = slice {slash + 1, path.size - scope.size - 1};
            } else {
                name = path;
            }
            return {name, scope};
        }

        inline static alloc_slice const kDefaultCollectionPath = collectionSpecToPath(kC4DefaultCollectionSpec, false);

        struct CollectionOptions
        {
            alloc_slice                         collectionPath;

            C4ReplicatorMode                    push;
            C4ReplicatorMode                    pull;

            fleece::AllocedDict                 properties;

            C4ReplicatorValidationFunction      pushFilter;
            C4ReplicatorValidationFunction      pullFilter;
            void*                               callbackContext;

            CollectionOptions(alloc_slice collectionPath_)
            {
                collectionPath = collectionPath_;
            }

            CollectionOptions(alloc_slice collectionPath_, C4Slice properties_)
            : properties(properties_)
            {
                collectionPath = collectionPath_;
            }

            template <class T>
            CollectionOptions& setProperty(fleece::slice name, T value) {
                properties = Options::updateProperties(properties, name, value);
                return *this;
            }
        };

        std::vector<CollectionOptions> collectionOpts;
        
        // Post-conditions:
        //   collectionOpts.size() > 0
        //   collectionAware == false if and only if collectionOpts.size() == 1 &&
        //                                           collectionOpts[0].collectionPath == defaultCollectionPath
        //   isActive == true ? all collections are active
        //                    : all collections are passive.
        inline void verify() const;

        size_t collectionCount() const {
            return collectionOpts.size();
        }

        Mode push(unsigned i) const {
            return collectionOpts[i].push;
        }

        Mode pull(unsigned i) const {
            return collectionOpts[i].pull;
        }

        void forEachCollection(function_ref<void(unsigned index,
                                const CollectionOptions&)> callback) const {
            for (unsigned i = 0; i < collectionOpts.size(); ++i) {
                callback(i, collectionOpts[i]);
            }
        }

    private:
        inline void setCollectionOptions(Mode push, Mode pull);
        inline void setCollectionOptions(C4ReplicatorParameters params);
        inline void setCollectionOptions(const Options& opt);
        inline void constructorCheck();
        
        mutable bool            _collectionAware         {true};
        mutable bool            _isActive                {true};
        mutable std::unordered_map<C4CollectionSpec, size_t> _collectionSpecToIndex;
    };

    inline void Options::setCollectionOptions(Mode push, Mode pull) {
        collectionOpts.reserve(1);
        auto& back = collectionOpts.emplace_back(kDefaultCollectionPath);
        back.push = push;
        back.pull = pull;
        _collectionAware = false;
    }

    inline void Options::setCollectionOptions(C4ReplicatorParameters params) {
        if (params.collectionCount == 0) {
            setCollectionOptions(params.push, params.pull);
            auto& back = collectionOpts.back();
            back.pushFilter = params.pushFilter;
            back.pullFilter = params.validationFunc;
            back.callbackContext = params.callbackContext;
            return;
        }

        // Assertion: params.collectionCount > 0
        collectionOpts.reserve(params.collectionCount);
        for (unsigned i = 0; i < params.collectionCount; ++i) {
            C4ReplicationCollection& c4Coll = params.collections[i];
            alloc_slice collPath = collectionSpecToPath(c4Coll.collection);
            auto& back = collectionOpts.emplace_back(collPath, c4Coll.optionsDictFleece);
            back.push = c4Coll.push;
            back.pull = c4Coll.pull;
            back.pushFilter = c4Coll.pushFilter;
            back.pullFilter = c4Coll.pullFilter;
            back.callbackContext = c4Coll.callbackContext;
        }
    }

    inline void Options::setCollectionOptions(const Options& opt) {
        Assert(opt.collectionOpts.size() > 0);
        collectionOpts.reserve(opt.collectionOpts.size());
        for (auto& collOpts : opt.collectionOpts) {
            auto& back = collectionOpts.emplace_back(collOpts.collectionPath, collOpts.properties.data());
            back.push = collOpts.push;
            back.pull = collOpts.pull;
            back.pushFilter = collOpts.pushFilter;
            back.pullFilter = collOpts.pullFilter;
            back.callbackContext = collOpts.callbackContext;
        }
    }

    inline void Options::verify() const {
        if (collectionOpts.size() == 0) {
            throw error(error::LiteCore, error::InvalidParameter,
                        "Invalid replicator configuration: requiring at least one collection");
        }

        for (size_t i = collectionOpts.size(); i-- > 0; ) {
            if (collectionOpts[i].push == kC4Disabled
                && collectionOpts[i].pull == kC4Disabled) {
                throw error(error::LiteCore, error::InvalidParameter,
                            "Invalid replicator configuration: a collection with both push and pull disabled");
            }
        }

        // Assertion: collectionOpts contains no disabled collections
        // (of which both push and pull are disabled)

        // Do not allow active and passive to be mixed in the same replicator.
        
        unsigned passCount = 0;
        unsigned actiCount = 0;
        for (auto c: collectionOpts) {
            if (c.push == kC4Passive)
                ++passCount;
            else if (c.push > kC4Passive)
                ++actiCount;
            if (c.pull == kC4Passive)
                ++passCount;
            else if (c.pull > kC4Passive)
                ++actiCount;

            if (passCount * actiCount > 0) {
                throw error(error::LiteCore, error::InvalidParameter,
                            "Invalid replicator configuration: the collection list includes"
                            " both passive and active ReplicatorMode");
            }
        }
        _isActive = actiCount > 0;

        // Do not mix one-shot and continous modes in one replicator.

        unsigned oneshot = 0;
        unsigned continuous = 0;
        if (_isActive && collectionOpts.size() > 1) {
            for (auto c: collectionOpts) {
                if (c.push == kC4OneShot)
                    ++oneshot;
                else if (c.push == kC4Continuous)
                    ++continuous;
                if (c.pull == kC4OneShot)
                    ++oneshot;
                else if (c.pull == kC4Continuous)
                    ++continuous;

                if (oneshot * continuous > 0) {
                    throw error(error::LiteCore, error::InvalidParameter,
                                "Invalid replicator configuration: kC4OneShot and kC4Continuous modes cannot be mised in one replicator.");
                }
            }
        }

        if (collectionOpts.size() == 1) {
            auto spec = collectionPathToSpec(collectionOpts[0].collectionPath);
            if (spec == kC4DefaultCollectionSpec) {
                _collectionAware = false;
            }
        }
    }

    // Post-conditions:
    //   - collectionOpts contains no duplicated collection.
    inline void Options::constructorCheck() {
        Assert(collectionOpts.size() < kNotCollectionIndex);

        // Create the mapping from CollectionSpec to the index to collctionOpts
        for (size_t i = 0; i < collectionOpts.size(); ++i) {
            auto spec = collectionPathToSpec(collectionOpts[i].collectionPath);
            auto [at, b] = _collectionSpecToIndex.insert(std::make_pair(spec, i));
            if (!b) {
                throw error(error::LiteCore, error::InvalidParameter,
                            "Invalid replicator configuration: the collection list contains duplicated collections.");
            }
        }
    }

} }
