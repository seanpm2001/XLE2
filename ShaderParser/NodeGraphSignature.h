// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <vector>

namespace GraphLanguage
{
    enum class ParameterDirection { In, Out };
    class NodeGraphSignature
    {
    public:
		class Parameter
		{
		public:
			std::string _type, _name;
			ParameterDirection _direction = ParameterDirection::In;
            std::string _semantic, _default;
		};

        // Returns the list of parameters taken as input through the function call mechanism
        auto GetParameters() const -> IteratorRange<const Parameter*>	{ return MakeIteratorRange(_functionParameters); }
		auto GetParameters() -> IteratorRange<Parameter*>	{ return MakeIteratorRange(_functionParameters); }
        void AddParameter(const Parameter& param);

        // Returns the list of parameters that are accesses as global scope variables (or captured from a containing scope)
        // In other words, these aren't explicitly passed to the function, but the function needs to interact with them, anyway
        auto GetCapturedParameters() const -> IteratorRange<const Parameter*>		{ return MakeIteratorRange(_capturedParameters); }
        void AddCapturedParameter(const Parameter& param);

        class TemplateParameter
        {
        public:
            std::string _name;
            std::string _restriction;
        };
        auto GetTemplateParameters() const -> IteratorRange<const TemplateParameter*>   { return MakeIteratorRange(_templateParameters); }
        void AddTemplateParameter(const TemplateParameter& param);

		const std::string& GetImplements() const { return _implements; }
		void SetImplements(const std::string& value) { _implements = value; }
		
        NodeGraphSignature();
        ~NodeGraphSignature();
    private:
        std::vector<Parameter> _functionParameters;
        std::vector<Parameter> _capturedParameters;
        std::vector<TemplateParameter> _templateParameters;
		std::string _implements;
    };

    class UniformBufferSignature
    {
    public:
        class Parameter
        {
        public:
            std::string _name;
			std::string _type;
            std::string _semantic;
        };

        std::vector<Parameter> _parameters;
    };

    class ShaderFragmentSignature
    {
    public:
        std::vector<std::pair<std::string, NodeGraphSignature>>			_functions;
        std::vector<std::pair<std::string, UniformBufferSignature>>		_uniformBuffers;
    };
}

