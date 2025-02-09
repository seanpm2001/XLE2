// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <utility>
#include <iosfwd>

namespace ShaderSourceParser { class InstantiationRequest; }
namespace Utility { template<typename Char> class InputStreamFormatter; class OutputStreamFormatter; }

namespace RenderCore { namespace Assets
{
	class ShaderPatchCollection
	{
	public:
		IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> GetPatches() const { return MakeIteratorRange(_patches); }
		const std::string& GetDescriptorSet() const { return _descriptorSet; }
		const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const { return _hash; }

		void MergeInto(ShaderPatchCollection& dest) const;

		void SerializeMethod(OutputStreamFormatter& formatter) const;

		friend bool operator<(const ShaderPatchCollection& lhs, const ShaderPatchCollection& rhs);
		friend bool operator<(const ShaderPatchCollection& lhs, uint64_t rhs);
		friend bool operator<(uint64_t lhs, const ShaderPatchCollection& rhs);

		ShaderPatchCollection();
		ShaderPatchCollection(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules&, const ::Assets::DepValPtr& depVal);
		ShaderPatchCollection(IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> patches);
		ShaderPatchCollection(std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>>&& patches);
		~ShaderPatchCollection();

	private:
		std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>> _patches;
		std::string _descriptorSet;
		uint64_t _hash = ~0ull;
		std::shared_ptr<::Assets::DependencyValidation> _depVal;

		void SortAndCalculateHash();
	};
	
	std::vector<ShaderPatchCollection> DeserializeShaderPatchCollectionSet(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules&, const ::Assets::DepValPtr& depVal);
	void SerializeShaderPatchCollectionSet(OutputStreamFormatter& formatter, IteratorRange<const ShaderPatchCollection*> patchCollections);

	std::ostream& operator<<(std::ostream& str, const ShaderPatchCollection& patchCollection);
}}

