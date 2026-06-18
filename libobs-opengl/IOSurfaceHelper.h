#pragma once

#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>

void writeIOSurfaceContents(IOSurfaceRef surface, NSString *filePath);
