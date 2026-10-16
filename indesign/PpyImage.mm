// A rendered page as a picture an agent can look at: scaled to at most 1024 px
// on the long side, on white, as a JPEG (PNG pages at print resolution are far
// too big to send back). No SDK; ImageIO does the work.

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include "PpyImage.h"

#include <algorithm>
#include <cmath>

peppy::Json PpyInlineImage(const std::string& path, int maxSide)
{
	NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
	CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef) url, nullptr);
	if (!source) throw peppy::Error(peppy::kErrHost, "couldn't read the rendered page");
	CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	CFRelease(source);
	if (!image) throw peppy::Error(peppy::kErrHost, "couldn't read the rendered page");

	size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
	double k = std::min(1.0, (double) maxSide / (double) std::max(w, h));
	size_t tw = std::max<size_t>(1, (size_t) std::lround(w * k)), th = std::max<size_t>(1, (size_t) std::lround(h * k));
	CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef ctx = CGBitmapContextCreate(nullptr, tw, th, 8, 0, rgb, kCGImageAlphaNoneSkipLast);
	CGColorSpaceRelease(rgb);
	CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
	CGContextFillRect(ctx, CGRectMake(0, 0, tw, th));
	CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
	CGContextDrawImage(ctx, CGRectMake(0, 0, tw, th), image);
	CGImageRelease(image);
	CGImageRef scaled = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);

	NSMutableData* jpeg = [NSMutableData data];
	CGImageDestinationRef dest = CGImageDestinationCreateWithData((__bridge CFMutableDataRef) jpeg, CFSTR("public.jpeg"), 1, nullptr);
	NSDictionary* opts = @{(__bridge NSString*) kCGImageDestinationLossyCompressionQuality: @0.85};
	CGImageDestinationAddImage(dest, scaled, (__bridge CFDictionaryRef) opts);
	bool ok = CGImageDestinationFinalize(dest);
	CFRelease(dest);
	CGImageRelease(scaled);
	if (!ok) throw peppy::Error(peppy::kErrHost, "couldn't encode the picture");

	return {{"mimeType", "image/jpeg"}, {"data", [jpeg base64EncodedStringWithOptions:0].UTF8String}, {"width", tw}, {"height", th}};
}
