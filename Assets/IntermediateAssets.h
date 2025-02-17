// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"

namespace Assets
{
	class DependentFileState;
	class DependencyValidation;
	class IAssetCompiler;
	class IArtifactCompileMarker;
	class IFileInterface;
}

namespace Utility { class OutputStream; class BasicFile; }

namespace Assets { namespace IntermediateAssets
{
	/// <summary>Archive of compiled intermediate assets</summary>
	/// When compile operations succeed, the resulting artifacts are cached in an IntermediateAssets::Store,
	/// which is typically in permanent memory (ie, on disk).
	///
	/// When working with multiple different versions of the engine codebase, it's necessary to have separate
	/// copies of the intermediate store (ie, because of changes to the data format, etc). This object provides
	/// the logic to select the correct store for the current codebase.
	///
	/// This make it easier to rapidly switch between different versions of the codebase, which can allow (for
	/// example) performance comparisons between different versions. Or, consider the case where we have 2
	/// executables (eg, a game executable and a GUI tool executable) which we want to use with the same 
	/// source assets, but they may have been compiled with different version of the engine code. This system
	/// allows both executables to maintain separate copies of the intermediate store.
	class Store
	{
	public:
		using DepVal = std::shared_ptr<DependencyValidation>;

		DepVal MakeDependencyValidation(
			StringSection<ResChar> intermediateFileName) const;

		DepVal WriteDependencies(
			StringSection<ResChar> intermediateFileName, 
			StringSection<ResChar> baseDir, 
			IteratorRange<const DependentFileState*> deps,
			bool makeDepValidation = true) const;

		void    MakeIntermediateName(
			ResChar buffer[], unsigned bufferMaxCount, 
			StringSection<ResChar> firstInitializer) const;

		template<int Count>
			void    MakeIntermediateName(ResChar (&buffer)[Count], StringSection<ResChar> firstInitializer) const
			{
				MakeIntermediateName(buffer, Count, firstInitializer);
			}

		static auto GetDependentFileState(StringSection<ResChar> filename) -> DependentFileState;
		static void ShadowFile(StringSection<ResChar> filename);

        static void ClearDependencyData();

		Store(const ResChar baseDirectory[], const ResChar versionString[], const ResChar configString[], bool universal = false);
		~Store();
		Store(const Store&) = delete;
		Store& operator=(const Store&) = delete;

	protected:
		mutable std::string _resolvedBaseDirectory;
		mutable std::unique_ptr<BasicFile> _markerFile;

		struct ConstructorOptions
		{
			::Assets::rstring _baseDir;
			::Assets::rstring _versionString;
			::Assets::rstring _configString;
		};
		ConstructorOptions _constructorOptions;

		void ResolveBaseDirectory() const;
	};
}}



