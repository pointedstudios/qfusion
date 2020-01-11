/*
Copyright (C) 1999 Stephen C. Taylor
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "materiallocal.h"

#include "../qcommon/links.h"

// TODO: Lift to the project toplevel
#include "../game/ai/static_vector.h"

template <typename T, unsigned MaxBins = 16, unsigned MaxMatchers = 32>
class EnumTokenMatcher {
	static_assert( MaxBins != 0 );
protected:
	struct TokenPattern {
		TokenPattern *next { nullptr };
		wsw::StringView name;
		T token;

		TokenPattern( const char *name_, T token_ ): name( name_ ), token( token_ ) {}

		[[nodiscard]]
		bool match( const wsw::StringView &v ) const {
			return name.equalsIgnoreCase( v );
		}
	};
private:
	TokenPattern *smallLenHeads[MaxBins] { nullptr };
	TokenPattern *largeLenHead { nullptr };

	StaticVector<TokenPattern, MaxMatchers> matchers;
protected:
	void add( const char *name, T value ) {
		matchers.emplace_back( { name, value } );

		TokenPattern *newPattern = &matchers.back();
		auto len = newPattern->name.length();
		assert( len );

		TokenPattern **head = &largeLenHead;
		if( len - 1 < MaxBins ) {
			head = &smallLenHeads[len - 1];
		}

		newPattern->next = *head;
		*head = newPattern;
	}

	[[nodiscard]]
	std::optional<T> matchInList( const TokenPattern *head, const wsw::StringView &v ) const {
		for( const auto *pattern = head; pattern; pattern = pattern->next ) {
			if( pattern->match( v ) ) {
				return std::make_optional( pattern->token );
			}
		}
		return std::nullopt;
	}
public:
	[[nodiscard]]
	std::optional<T> match( const wsw::StringView &v ) const {
		auto len = v.length();
		if( !len ) {
			return std::nullopt;
		}
		if( len - 1 < MaxBins ) {
			return matchInList( smallLenHeads[len - 1], v );
		}
		return matchInList( largeLenHead, v );
	}
};

class DeformTokenMatcher : public EnumTokenMatcher<Deform> {
public:
	DeformTokenMatcher() noexcept {
		add( "Wave", Deform::Wave );
		add( "Bulge", Deform::Bulge );
		add( "Move", Deform::Move );
		add( "Autosprite", Deform::Autosprite );
		add( "Autosprite2", Deform::Autosprite2 );
		add( "Autoparticle", Deform::Autoparticle );
	}
};

static DeformTokenMatcher deformTokenMatcher;

class FuncTokenMatcher : public EnumTokenMatcher<Func> {
public:
	FuncTokenMatcher() noexcept {
		add( "Sin", Func::Sin );
		add( "Triangle", Func::Triangle );
		add( "Square", Func::Square );
		add( "Sawtooth", Func::Sawtooth );
		add( "InvSawtooth", Func::InvSawtooth );
		add( "Noize", Func::Noize );
		add( "DistanceRamp", Func::DistanceRamp );
	}
};

static FuncTokenMatcher funcTokenMatcher;

class PassKeyMatcher : public EnumTokenMatcher<PassKey> {
public:
	PassKeyMatcher() noexcept {
		add( "RgbGen", PassKey::RgbGen );
		add( "BlendFunc", PassKey::BlendFunc );
		add( "DepthFunc", PassKey::DepthFunc );
		add( "DepthWrite", PassKey::DepthWrite );
		add( "AlphaFunc", PassKey::AlphaFunc );
		add( "TCMod", PassKey::TCMod );
		add( "Map", PassKey::Map );
		add( "AnimMap", PassKey::AnimMap );
		add( "CubeMap", PassKey::CubeMap );
		add( "ShadeCubeMap", PassKey::ShadeCubeMap );
		add( "ClampMap", PassKey::ClampMap );
		add( "AnimClampMap", PassKey::AnimClampMap );
		add( "Material", PassKey::Material );
		add( "Distortion", PassKey::Distortion );
		add( "CelShade", PassKey::CelShade );
		add( "TCGen", PassKey::TCGen );
		add( "AlphaGen", PassKey::AlphaGen );
		add( "Detail", PassKey::Detail );
		add( "Greyscale", PassKey::Grayscale );
		add( "Grayscale", PassKey::Grayscale );
		add( "Skip", PassKey::Skip );
	}
};

static PassKeyMatcher passKeyMatcher;

class IntConditionVarMatcher : public EnumTokenMatcher<IntConditionVar> {
public:
	IntConditionVarMatcher() noexcept {
		add( "MaxTextureSize", IntConditionVar::MaxTextureSize );
		add( "MaxTextureCubemapSize", IntConditionVar::MaxTextureCubemapSize );
		add( "MaxTextureUnits", IntConditionVar::MaxTextureUnits );
	}
};

static IntConditionVarMatcher intVarMatcher;

class BoolConditionVarMatcher : public EnumTokenMatcher<BoolConditionVar> {
public:
	BoolConditionVarMatcher() noexcept {
		add( "TextureCubeMap", BoolConditionVar::TextureCubeMap );
		add( "Glsl", BoolConditionVar::Glsl );
		add( "Deluxe", BoolConditionVar::DeluxeMaps );
		add( "DeluxeMaps", BoolConditionVar::DeluxeMaps );
		add( "PortalMaps", BoolConditionVar::PortalMaps );
	}
};

static BoolConditionVarMatcher boolVarMatcher;

class LogicOpMatcher : public EnumTokenMatcher<LogicOp, 2> {
public:
	LogicOpMatcher() noexcept {
		add( "&&", LogicOp::And );
		add( "||", LogicOp::Or );
	}
};

static LogicOpMatcher logicOpMatcher;

class CmpOpMatcher : public EnumTokenMatcher<CmpOp, 2> {
public:
	CmpOpMatcher() noexcept {
		add( "<", CmpOp::LS );
		add( "<=", CmpOp::LE );
		add( ">", CmpOp::GT );
		add( ">=", CmpOp::GE );
		add( "!=", CmpOp::NE );
		add( "==", CmpOp::EQ );
	}
};

static CmpOpMatcher cmpOpMatcher;

class CullModeMatcher : public EnumTokenMatcher<CullMode> {
public:
	CullModeMatcher() noexcept {
		add( "None", CullMode::None );
		add( "Disable", CullMode::None );
		add( "Twosided", CullMode::None );

		add( "Back", CullMode::Back );
		add( "Backside", CullMode::Back );
		add( "Backsided", CullMode::Back );

		add( "Front", CullMode::Front );
	}
};

static CullModeMatcher cullModeMatcher;

class SortModeMatcher : public EnumTokenMatcher<SortMode> {
public:
	SortModeMatcher() noexcept {
		add( "Portal", SortMode::Portal );
		add( "Sky", SortMode::Sky );
		add( "Opaque", SortMode::Opaque );
		add( "Banner", SortMode::Banner );
		add( "Underwater", SortMode::Underwater );
		add( "Additive", SortMode::Additive );
		add( "Nearest", SortMode::Nearest );
	}
};

static SortModeMatcher sortModeMatcher;

class MaterialKeyParser : public EnumTokenMatcher<MaterialKey> {
public:
	MaterialKeyParser() noexcept {
		add( "Cull", MaterialKey::Cull );
		add( "SkyParams", MaterialKey::SkyParams );
		add( "SkyParams2", MaterialKey::SkyParams2 );
		add( "SkyParamsSides", MaterialKey::SkyParamsSides );
		add( "FogParams", MaterialKey::FogParams );
		add( "NoMipMaps", MaterialKey::NoMipMaps );
		add( "NoPicMip", MaterialKey::NoPicMip );
		add( "NoCompress", MaterialKey::NoCompress );
		add( "NoFiltering", MaterialKey::NoFiltering );
		add( "SmallestMipSize", MaterialKey::SmallestMipSize );
		add( "PolygonOffset", MaterialKey::PolygonOffset );
		add( "StencilTest", MaterialKey::StencilTest );
		add( "Sort", MaterialKey::Sort );
		add( "DeformVertexes", MaterialKey::DeformVertexes );
		add( "Portal", MaterialKey::Portal );
		add( "EntityMergable", MaterialKey::EntityMergable );
		add( "If", MaterialKey::If );
		add( "EndIf", MaterialKey::EndIf );
		add( "OffsetMappingScale", MaterialKey::OffsetMappingScale );
		add( "GlossExponent", MaterialKey::GlossExponent );
		add( "GlossIntensity", MaterialKey::GlossIntensity );
		add( "Template", MaterialKey::Template );
		add( "Skip", MaterialKey::Skip );
		add( "SoftParticle", MaterialKey::SoftParticle );
		add( "ForceWorldOutlines", MaterialKey::ForceWorldOutlines );
	}
};

static MaterialKeyParser materialKeyParser;

class RgbGenMatcher : public EnumTokenMatcher<RgbGen> {
public:
	RgbGenMatcher() noexcept {
		add( "Identity", RgbGen::Identity );
		add( "IdentityLighting", RgbGen::Identity );
		add( "Wave", RgbGen::Wave );
		add( "ColorWave", RgbGen::ColorWave );
		add( "Custom", RgbGen::Custom );
		add( "TeamColor", RgbGen::Custom );
		add( "CustomColorWave", RgbGen::Custom );
		add( "TeamColorWave", RgbGen::CustomWave );
		add( "Entity", RgbGen::Entity );
		add( "EntityWave", RgbGen::EntityWave );
		add( "OneMinusEntity", RgbGen::OneMinusEntity );
		add( "Vertex", RgbGen::Vertex );
		add( "OneMinusVertex", RgbGen::OneMinusVertex );
		add( "LightingDiffuse", RgbGen::LightingDiffuse );
		add( "ExactVertex", RgbGen::ExactVertex );
		add( "Const", RgbGen::Const );
		add( "Constant", RgbGen::Const );
	}
};

static RgbGenMatcher rgbGenMatcher;

class AlphaGenMatcher : public EnumTokenMatcher<AlphaGen> {
public:
	AlphaGenMatcher() noexcept {
		add( "Vertex", AlphaGen::Vertex );
		add( "OneMinusVertex", AlphaGen::OneMinusVertex );
		add( "Entity", AlphaGen::Entity );
		add( "Wave", AlphaGen::Wave );
		add( "Const", AlphaGen::Const );
		add( "Constant", AlphaGen::Const );
	}
};

static AlphaGenMatcher alphaGenMatcher;

class SrcBlendMatcher : public EnumTokenMatcher<SrcBlend> {
public:
	SrcBlendMatcher() noexcept {
		add( "GL_zero", SrcBlend::Zero );
		add( "GL_one", SrcBlend::One );
		add( "GL_dst_color", SrcBlend::DstColor );
		add( "GL_one_minus_dst_color", SrcBlend::OneMinusDstColor );
		add( "GL_src_alpha", SrcBlend::SrcAlpha );
		add( "GL_one_minus_src_alpha", SrcBlend::OneMinusSrcAlpha );
		add( "GL_dst_alpha", SrcBlend::DstAlpha );
		add( "GL_one_minus_dst_alpha", SrcBlend::OneMinusDstAlpha );
	}
};

static SrcBlendMatcher srcBlendMatcher;

class DstBlendMatcher : public EnumTokenMatcher<DstBlend> {
public:
	DstBlendMatcher() noexcept {
		add( "GL_zero", DstBlend::Zero );
		add( "GL_one", DstBlend::One );
		add( "GL_src_color", DstBlend::SrcColor );
		add( "GL_one_minus_src_color", DstBlend::OneMinusSrcColor );
		add( "GL_src_alpha", DstBlend::SrcAlpha );
		add( "GL_one_minus_src_alpha", DstBlend::OneMinusSrcAlpha );
		add( "GL_dst_alpha", DstBlend::DstAlpha );
		add( "GL_one_minus_dst_alpha", DstBlend::OneMinusDstAlpha );
	}
};

static DstBlendMatcher dstBlendMatcher;

class UnaryBlendFuncMatcher : public EnumTokenMatcher<UnaryBlendFunc> {
public:
	UnaryBlendFuncMatcher() noexcept {
		add( "Blend", UnaryBlendFunc::Blend );
		add( "Filter", UnaryBlendFunc::Filter );
		add( "Add", UnaryBlendFunc::Add );
	}
};

static UnaryBlendFuncMatcher unaryBlendFuncMatcher;

class AlphaFuncMatcher : public EnumTokenMatcher<AlphaFunc> {
public:
	AlphaFuncMatcher() noexcept {
		add( "Gt0", AlphaFunc::GT0 );
		add( "Lt128", AlphaFunc::LT128 );
		add( "Ge128", AlphaFunc::GE128 );
	}
};

static AlphaFuncMatcher alphaFuncMatcher;

class DepthFuncMatcher : public EnumTokenMatcher<DepthFunc> {
public:
	DepthFuncMatcher() noexcept {
		add( "Equal", DepthFunc::EQ );
		add( "Greater", DepthFunc::GT );
	}
};

static DepthFuncMatcher depthFuncMatcher;

class TCModMatcher : public EnumTokenMatcher<TCMod> {
public:
	TCModMatcher() noexcept {
		add( "Rotate", TCMod::Rotate );
		add( "Scale", TCMod::Scale );
		add( "Scroll", TCMod::Scroll );
		add( "Stretch", TCMod::Stretch );
		add( "Transform", TCMod::Transform );
		add( "Turb", TCMod::Turb );
	}
};

static TCModMatcher tcModMatcher;

class TCGenMatcher : public EnumTokenMatcher<TCGen> {
public:
	TCGenMatcher() noexcept {
		add( "Base", TCGen::Base );
		add( "Lightmap", TCGen::Lightmap );
		add( "Environment", TCGen::Environment );
		add( "Vector", TCGen::Vector );
		add( "Reflection", TCGen::Reflection );
		add( "Celshade", TCGen::Celshade );
		add( "Surround", TCGen::Surround );
	}
};

static TCGenMatcher tcGenMatcher;

class SkySideMatcher : public EnumTokenMatcher<SkySide> {
public:
	SkySideMatcher() noexcept {
		add( "Rt", SkySide::Right );
		add( "Bk", SkySide::Back );
		add( "Lf", SkySide::Left );
		add( "Rt", SkySide::Right );
		add( "Up", SkySide::Up );
		add( "Dn", SkySide::Down );
	}
};

static SkySideMatcher skySideMatcher;

#define IMPLEMENT_GET_ENUM_METHOD( type, method, matcher ) \
std::optional<type> MaterialLexer::method() {\
	if( auto token = getNextTokenInLine() ) {\
		if ( auto func = ::matcher.match( *token ) ) {\
			return func;\
		}\
		unGetToken();\
	}\
	return std::nullopt;\
}

IMPLEMENT_GET_ENUM_METHOD( Func, getFunc, funcTokenMatcher )
IMPLEMENT_GET_ENUM_METHOD( Deform, getDeform, deformTokenMatcher )
IMPLEMENT_GET_ENUM_METHOD( PassKey, getPassKey, passKeyMatcher )
IMPLEMENT_GET_ENUM_METHOD( IntConditionVar, getIntConditionVar, intVarMatcher )
IMPLEMENT_GET_ENUM_METHOD( BoolConditionVar, getBoolConditionVar, boolVarMatcher )
IMPLEMENT_GET_ENUM_METHOD( LogicOp, getLogicOp, logicOpMatcher )
IMPLEMENT_GET_ENUM_METHOD( CmpOp, getCmpOp, cmpOpMatcher )
IMPLEMENT_GET_ENUM_METHOD( CullMode, getCullMode, cullModeMatcher )
IMPLEMENT_GET_ENUM_METHOD( SortMode, getSortMode, sortModeMatcher )
IMPLEMENT_GET_ENUM_METHOD( MaterialKey, getMaterialKey, materialKeyParser )
IMPLEMENT_GET_ENUM_METHOD( RgbGen, getRgbGen, rgbGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( AlphaGen, getAlphaGen, alphaGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( SrcBlend, getSrcBlend, srcBlendMatcher )
IMPLEMENT_GET_ENUM_METHOD( DstBlend, getDstBlend, dstBlendMatcher )
IMPLEMENT_GET_ENUM_METHOD( UnaryBlendFunc, getUnaryBlendFunc, unaryBlendFuncMatcher )
IMPLEMENT_GET_ENUM_METHOD( AlphaFunc, getAlphaFunc, alphaFuncMatcher )
IMPLEMENT_GET_ENUM_METHOD( DepthFunc, getDepthFunc, depthFuncMatcher )
IMPLEMENT_GET_ENUM_METHOD( TCMod, getTCMod, tcModMatcher )
IMPLEMENT_GET_ENUM_METHOD( TCGen, getTCGen, tcGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( SkySide, getSkySide, skySideMatcher )

static const wsw::StringView kTrueLiteral( "true" );
static const wsw::StringView kFalseLiteral( "false" );

std::optional<bool> MaterialLexer::getBool() {
	if( auto maybeToken = getNextToken() ) {
		if( kTrueLiteral.equalsIgnoreCase( *maybeToken ) ) {
			return true;
		}
		if( kFalseLiteral.equalsIgnoreCase( *maybeToken ) ) {
			return false;
		}
	}
	return std::nullopt;
}

bool MaterialLexer::parseVector( float *dest, size_t numElems ) {
	assert( numElems > 1 && numElems <= 8 );
	float scratchpad[8];

	bool hadParenAtStart = false;
	if( auto maybeFirstToken = getNextTokenInLine() ) {
		auto token = *maybeFirstToken;
		if( token.equals( wsw::StringView( "(" ) ) ) {
			hadParenAtStart = true;
		} else if( !unGetToken() ) {
			return false;
		}
	}

	for( size_t i = 0; i < numElems; ++i ) {
		if( auto maybeFloat = getFloat() ) {
			scratchpad[i] = *maybeFloat;
		} else {
			return false;
		}
	}

	// Modify the dest array if and only if parsing has succeeded

	if( !hadParenAtStart ) {
		std::copy( scratchpad, scratchpad + numElems, dest );
		return true;
	}

	if( auto maybeNextToken = getNextTokenInLine() ) {
		auto token = *maybeNextToken;
		if( token.equals( wsw::StringView( ")" ) ) ) {
			std::copy( scratchpad, scratchpad + numElems, dest );
			return true;
		}
	}

	return false;
}

void MaterialLexer::parseVectorOrFill( float *dest, size_t numElems, float defaultValue ) {
	assert( numElems > 1 && numElems <= 8 );

	bool hadParenAtStart = false;
	if( auto maybeFirstToken = getNextTokenInLine() ) {
		if( ( *maybeFirstToken ).equals( wsw::StringView( "(" ) ) ) {
			hadParenAtStart = true;
		}
	}

	size_t i = 0;
	for(; i < numElems; ++i ) {
		if( auto maybeFloat = getFloat() ) {
			dest[i] = *maybeFloat;
		} else {
			break;
		}
	}

	std::fill( dest + i, dest + numElems, defaultValue );

	if( hadParenAtStart ) {
		if( auto maybeNextToken = getNextTokenInLine() ) {
			if( !( *maybeNextToken ).equals( wsw::StringView( ")" ) ) ) {
				unGetToken();
			}
		}
	}
}

bool MaterialLexer::skipToEndOfLine() {
	// Could be optimized but it gets called rarely (TODO: Really?)
	for(;; ) {
		auto maybeToken = getNextTokenInLine();
		if( !maybeToken ) {
			return true;
		}
	}
}

std::optional<std::pair<unsigned, unsigned>> TokenSplitter::fetchNextTokenInLine() {
	const char *__restrict p = data + offset;

start:
	// Strip whitespace characters until a non-whitespace one or a newline character is met
	for(;; p++ ) {
		if( !isspace( *p ) ) {
			break;
		}
		if( *p != '\n' && *p != '\r' ) {
			continue;
		}
		// Strip newline characters
		p++;
		while( *p && ( *p == '\n' || *p == '\r' ) ) {
			p++;
		}
		offset = p - data;
		return std::nullopt;
	}

	if( !*p ) {
		offset = p - data;
		return std::nullopt;
	}

	if( *p == '/' ) {
		if( p[1] == '/' ) {
			// Skip till end of line
			while (*p && (*p != '\n' && *p != '\r')) {
				p++;
			}
			// Strip newline at the end
			while (*p && (*p == '\n' || *p == '\r')) {
				p++;
			}
			offset = p - data;
			return std::nullopt;
		}

		if( p[1] == '*' ) {
			bool metNewline = false;
			// Skip till "*/" is met
			for(;; p++) {
				if( !*p ) {
					offset = p - data;
					return std::nullopt;
				}
				// TODO: Should we mark newlines met?
				if( *p == '*' ) {
					if( p[1] == '/' ) {
						p += 2;
						offset = p - data;
						break;
					}
				}
				metNewline |= ( *p == '\n' ) | ( *p == '\r' );
			}
			if( metNewline ) {
				offset = p - data;
				return std::nullopt;
			}
			// We may just recurse but this can lead to an overflow at bogus files with tons of comments
			goto start;
		}
	}

	if( *p == '"' ) {
		p++;
		const char *tokenStart = p;
		for(;; p++ ) {
			// TODO: What if '\n', '\r' (as a single byte) are met inside a string?
			if( !*p || *p == '"' ) {
				offset = p - data + 1;
				// What if a string is empty?
				return std::make_pair( tokenStart - data, p - tokenStart );
			}
		}
	}

	if( auto maybeSpanLen = tryMatching1Or2CharsToken( p ) ) {
		auto len = *maybeSpanLen;
		offset = ( p - data ) + len;
		return std::make_pair( p - data, len );
	}

	const char *tokenStart = p;
	for(;; p++ ) {
		if( !mustCloseTokenAtChar( p[0], p[1] ) ) {
			continue;
		}
		offset = p - data;
		auto len = p - tokenStart;
		assert( len >= 0 );
		return std::make_pair( tokenStart - data, len );
	}
}

std::optional<unsigned> TokenSplitter::tryMatching1Or2CharsToken( const char *tokenStart ) const {
	char ch = tokenStart[0];

	if( ch == '{' || ch == '}' || ch == '(' || ch == ')' ) {
		return 1;
	}

	if( ch == '<' || ch == '>' || ch == '!' ) {
		return ( tokenStart[1] == '=' ) ? 2 : 1;
	}

	if( ch == '=' && tokenStart[1] == '=' ) {
		return 2;
	}

	return std::nullopt;
}

bool TokenSplitter::mustCloseTokenAtChar( char ch, char nextCh ) {
	if( ::isspace( ch ) ) {
		return true;
	}
	if( ch == '\0' || ch == '"' ) {
		return true;
	}
	if( ch == '/' && ( nextCh == '/' || nextCh == '*' ) ) {
		return true;
	}
	if( ch == '{' || ch == '}' || ch == '(' || ch == ')' ) {
		return true;
	}
	if( ch == '>' || ch == '<' || ch == '!' ) {
		return true;
	}
	return ch == '=' && nextCh == '=';
}