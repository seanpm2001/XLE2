// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(COMMON_SHAPES_H)
#define COMMON_SHAPES_H

#include "Interfaces.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
class RoundedRectShape : IShape2D
{
    ShapeResult Calculate(
        DebuggingShapesCoords coords,
        ShapeDesc desc)
    {
        float2 texCoord = VSOUT_GetTexCoord0(coords);
        float2 minCoords = desc._minCoords, maxCoords = desc._maxCoords;
        [branch] if (
                texCoord.x < minCoords.x || texCoord.x > maxCoords.x
            ||	texCoord.y < minCoords.y || texCoord.y > maxCoords.y) {
            return ShapeResult_Empty();
        }

        float borderSizePix = desc._borderSizePix;
        float roundedProportion = desc._param0;

        float2 pixelSize = float2(GetUDDS(coords).x, GetVDDS(coords).y);
        float2 borderSize = borderSizePix * pixelSize;

        float roundedHeight = (maxCoords.y - minCoords.y) * roundedProportion;
        float roundedWidth = roundedHeight * GetAspectRatio(coords);

            // mirror coords so we only have to consider the top/left quadrant
        float2 r = float2(
            min(maxCoords.x - texCoord.x, texCoord.x) - minCoords.x,
            min(maxCoords.y - texCoord.y, texCoord.y) - minCoords.y);

        [branch] if (r.x < roundedWidth && r.y < roundedHeight) {
            float2 centre = float2(roundedWidth, roundedHeight);

            ////////////////
                //  To get a anti-aliased look to the edges, we need to make
                //  several samples. Lets just use a simple pattern aligned
                //  to the pixel edges...
            float2 samplePts[4] =
            {
                float2(.5f, .2f), float2(.5f, .8f),
                float2(.2f, .5f), float2(.8f, .5f),
            };

            ShapeResult result = ShapeResult_Empty();
            [unroll] for (uint c=0; c<4; ++c) {
                float2 o = r - centre + samplePts[c] * pixelSize;
                o.x /= GetAspectRatio(coords);
                float dist = roundedHeight - length(o);
                result._border += .25f * (dist >= 0.f && dist < borderSize.y);
                // result._fill = max(result._fill, dist >= borderSize.y);
                result._fill +=  .25f * (dist >= 0.f);
            }
            return result;
        }
        if (r.x <= borderSize.x || r.y <= borderSize.y) {
            return MakeShapeResult(1.f, 1.f);
        }

        return MakeShapeResult(1.f, 0.f);
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
float CircleShape2(float2 centrePoint, float radius, float2 texCoord, float aspectRatio)
{
    float2 o = texCoord - centrePoint;
    o.x /= aspectRatio;
    return dot(o, o) <= (radius*radius);
}

class RectShape : IShape2D
{
    ShapeResult Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
    {
            // we'll assume pixel-perfect coords, so we don't have handle
            // partially covered pixels on the edges.
        float2 texCoord = VSOUT_GetTexCoord0(coords);
        float2 minCoords = shapeDesc._minCoords, maxCoords = shapeDesc._maxCoords;
        bool fill =
               texCoord.x >= minCoords.x && texCoord.x < maxCoords.x
            && texCoord.y >= minCoords.y && texCoord.y < maxCoords.y;

        float2 r = float2(
            min(maxCoords.x - texCoord.x, texCoord.x) - minCoords.x,
            min(maxCoords.y - texCoord.y, texCoord.y) - minCoords.y);
        bool border = (texCoord.x <= GetUDDS(coords).x) || (texCoord.y <= GetVDDS(coords).y);

        return MakeShapeResult(float(fill), float(border));
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
class ScrollBarShape : IShape2D
{
    ShapeResult Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
    {
        float2 minCoords = shapeDesc._minCoords;
        float2 maxCoords = shapeDesc._maxCoords;
        float thumbPosition = shapeDesc._param0;
        float2 texCoord = VSOUT_GetTexCoord0(coords);
        float aspectRatio = GetAspectRatio(coords);

        float2 baseLineMin = float2(minCoords.x, lerp(minCoords.y, maxCoords.y, 0.4f));
        float2 baseLineMax = float2(maxCoords.x, lerp(minCoords.y, maxCoords.y, 0.6f));
        //float result = 0.5f * RoundedRectShape(baseLineMin, baseLineMax, texCoord, aspectRatio, 0.4f);
        RoundedRectShape rrs;
        float result = 0.5f * rrs.Calculate(coords, MakeShapeDesc(baseLineMin, baseLineMax, 0.f, 0.4f))._fill;

            //	Add small markers at fractional positions along the scroll bar
        float markerPositions[7] = { .125f, .25f, .375f, .5f,   .625f, .75f, .875f };
        float markerHeights[7]   = { .5f  , .75f, .5f ,  .825f, .5f,   .75f, .5f   };

        RectShape rect;
        for (uint c=0; c<7; ++c) {
            float x = lerp(minCoords.x, maxCoords.x, markerPositions[c]);
            float2 markerMin = float2(x - 0.002f, lerp(minCoords.y, maxCoords.y, 0.5f*(1.f-markerHeights[c])));
            float2 markerMax = float2(x + 0.002f, lerp(minCoords.y, maxCoords.y, 0.5f+0.5f*markerHeights[c]));
            // result = max(result, 0.75f*RectShape(markerMin, markerMax, texCoord));
            result = max(result, 0.75f*rect.Calculate(coords, MakeShapeDesc(markerMin, markerMax, 0.f, 0.f))._fill);
        }

        float2 thumbCenter = float2(
            lerp(minCoords.x, maxCoords.x, thumbPosition),
            lerp(minCoords.y, maxCoords.y, 0.5f));
        result = max(result, CircleShape2(thumbCenter, 0.475f * (maxCoords.y - minCoords.y), texCoord, aspectRatio));
        return MakeShapeResult(result, 0.f);
    }
};


#endif
