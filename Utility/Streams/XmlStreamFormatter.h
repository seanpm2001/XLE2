// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StreamFormatter.h"        // (for FormatException and StreamLocation)
#include <stack>

namespace Utility
{
    template<typename CharType>
        class TextStreamMarker
    {
    public:
        CharType operator*() const                      { return *_ptr; }
        CharType operator[](size_t offset) const        { assert((_ptr+offset) < _end); return *(_ptr+offset); }
        ptrdiff_t Remaining() const                     { return (_end - _ptr); }
        const TextStreamMarker<CharType>& operator++()  { _ptr++; assert(_ptr<=_end); return *this; }
        const CharType* Pointer() const                 { return _ptr; }
        void SetPointer(const CharType* newPtr)         { assert(newPtr <= _end); _ptr = newPtr; }

        StreamLocation GetLocation() const;
        void AdvanceCheckNewLine();

        TextStreamMarker(const MemoryMappedInputStream& stream);
        ~TextStreamMarker();
    protected:
        const CharType* _ptr;
        const CharType* _end;

        unsigned _lineIndex;
        const CharType* _lineStart;
    };

    /// <summary>Deserializes element and attribute data from xml</summary>
    /// This is an input deserializer for xml data that handles just element and
    /// attributes. The interface is compatible with InputStreamFormatter, and
    /// can be used as a drop-in replacement when required.
    /// 
    /// It's a hand-written perform oriented parser. It should perform reasonable
    /// well even for large files.
    ///
    /// Note that this is a subset of true xml. Many XML features (like processing
    /// instructions, references and character data) aren't fully supported. But it will
    /// read elements and attributes -- handy for applications of XML that use only 
    /// these things. There is some support for reading character data. But it is limited
    /// and intended for simple tasks.
    template<typename CharType>
        class XL_UTILITY_API XmlInputStreamFormatter
    {
    public:
        enum class Blob { BeginElement, EndElement, AttributeName, AttributeValue, CharacterData, None };
        Blob PeekNext();

        using InteriorSection = StringSection<CharType>;

        bool TryBeginElement(InteriorSection& name);
        bool TryEndElement();
        bool TryAttribute(InteriorSection& name, InteriorSection& value);
        bool TryCharacterData(InteriorSection& cdata);

        void SkipElement();

        StreamLocation GetLocation() const;

        using value_type = CharType;

        bool _allowCharacterData = false;

        XmlInputStreamFormatter(const TextStreamMarker<CharType>& marker);
        ~XmlInputStreamFormatter();
    protected:
        TextStreamMarker<CharType> _marker;
        Blob _primed;

        bool _pendingHeader;

        class Scope
        {
        public:
            enum class Type { AttributeList, Element, None };
            Type _type;
            InteriorSection _elementName;
        };

        std::stack<Scope> _scopeStack;
    };
}

using namespace Utility;
