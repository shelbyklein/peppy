#pragma once

#include "../shared/Core.h"

// {mimeType, data (base64), width, height}: the image at 'path' as a JPEG on
// white, at most maxSide px on its long side. Throws peppy::Error.
peppy::Json PpyInlineImage(const std::string& path, int maxSide = 1024);
