//
//  main.m
//  Moonlight TV
//
//  Created by Diego Waxemberg on 8/25/18.
//  Copyright © 2018 Moonlight Game Streaming Project. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "../Limelight/Stream/DecoderProbe.h"

#define SDL_MAIN_HANDLED
#import <SDL.h>

int main(int argc, char * argv[]) {
    @autoreleasepool {
        // Diagnostic: launch with -DecoderProbe to print which video formats this
        // device can decode, then exit without starting the app.
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-DecoderProbe") == 0) {
                fputs([RunDecoderProbe() UTF8String], stdout);
                fflush(stdout);
                return 0;
            }
        }

        SDL_SetMainReady();
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
