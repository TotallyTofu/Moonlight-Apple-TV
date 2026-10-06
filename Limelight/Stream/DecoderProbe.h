//
//  DecoderProbe.h
//  Moonlight-AV1
//
//  Diagnostic that asks VideoToolbox, on the device it runs on, which video formats it
//  can really decode. It feeds one real keyframe of each format to a decompression
//  session, first requiring a hardware decoder and then allowing any decoder.
//
//  Run it with the launch argument -DecoderProbe (see Moonlight TV/main.m). It prints the
//  report to stdout and exits without starting the app. DecoderProbeHardwareDecodes() is
//  also used before a stream to decide whether to offer YUV 4:4:4 to the host.
//
//  Header-only on purpose so it needs no project file changes. Every function is static
//  and marked unused, since each file that includes this uses only some of them.
//

#pragma once

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>
#include <sys/utsname.h>

#include <Limelight.h>

#include "DecoderProbeFrames.h"

#define PROBE_FN static __attribute__((unused))

typedef NS_ENUM(int, ProbeCodec) {
    ProbeCodecH264,
    ProbeCodecHEVC,
};

typedef struct {
    const char* name;
    ProbeCodec codec;
    const uint8_t* data;
    size_t length;
} ProbeCase;

typedef struct {
    OSStatus formatStatus;    // creating the format description
    OSStatus createStatus;    // creating the decompression session
    OSStatus decodeStatus;    // submitting the frame
    OSStatus outputStatus;    // status delivered with the decoded frame
    BOOL gotImage;
    BOOL usingHardware;
    OSType pixelFormat;
    size_t width, height;
} ProbeResult;

PROBE_FN NSString* ProbeFourCC(OSType t) {
    char c[5] = { (char)(t >> 24), (char)(t >> 16), (char)(t >> 8), (char)t, 0 };
    for (int i = 0; i < 4; i++) {
        if (c[i] < 32 || c[i] > 126) return [NSString stringWithFormat:@"0x%08x", (unsigned)t];
    }
    return [NSString stringWithFormat:@"'%s'", c];
}

// Split an Annex B stream into NAL units without their start codes
PROBE_FN NSArray<NSData*>* ProbeSplitAnnexB(const uint8_t* d, size_t len) {
    NSMutableArray<NSData*>* nals = [NSMutableArray array];
    long start = -1;
    for (size_t i = 0; i + 3 <= len; i++) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if (start >= 0) {
                size_t end = (i > (size_t)start && d[i - 1] == 0) ? i - 1 : i;
                [nals addObject:[NSData dataWithBytes:d + start length:end - start]];
            }
            start = (long)i + 3;
            i += 2;
        }
    }
    if (start >= 0 && (size_t)start < len) {
        [nals addObject:[NSData dataWithBytes:d + start length:len - start]];
    }
    return nals;
}

PROBE_FN int ProbeNalType(ProbeCodec codec, const uint8_t* nal) {
    return codec == ProbeCodecH264 ? (nal[0] & 0x1f) : ((nal[0] >> 1) & 0x3f);
}

PROBE_FN BOOL ProbeIsParameterSet(ProbeCodec codec, int type) {
    return codec == ProbeCodecH264 ? (type == 7 || type == 8) : (type >= 32 && type <= 34);
}

PROBE_FN BOOL ProbeIsPicture(ProbeCodec codec, int type) {
    return codec == ProbeCodecH264 ? (type >= 1 && type <= 5) : (type >= 0 && type <= 31);
}

PROBE_FN ProbeResult ProbeDecode(const ProbeCase* c, BOOL requireHardware) {
    ProbeResult r = {};
    r.formatStatus = r.createStatus = r.decodeStatus = r.outputStatus = -1;

    NSArray<NSData*>* nals = ProbeSplitAnnexB(c->data, c->length);
    NSMutableArray<NSData*>* paramSets = [NSMutableArray array];
    NSMutableArray<NSData*>* pictures = [NSMutableArray array];
    for (NSData* nal in nals) {
        if (nal.length < 2) continue;
        int type = ProbeNalType(c->codec, nal.bytes);
        if (ProbeIsParameterSet(c->codec, type)) [paramSets addObject:nal];
        else if (ProbeIsPicture(c->codec, type)) [pictures addObject:nal];
    }
    if (paramSets.count == 0 || pictures.count == 0) {
        r.formatStatus = -2; // malformed test data
        return r;
    }

    const uint8_t* ptrs[8];
    size_t sizes[8];
    size_t n = MIN(paramSets.count, (NSUInteger)8);
    for (size_t i = 0; i < n; i++) {
        ptrs[i] = paramSets[i].bytes;
        sizes[i] = paramSets[i].length;
    }

    CMVideoFormatDescriptionRef fd = NULL;
    if (c->codec == ProbeCodecH264) {
        r.formatStatus = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, n, ptrs, sizes, 4, &fd);
    } else {
        r.formatStatus = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, n, ptrs, sizes, 4, NULL, &fd);
    }
    if (r.formatStatus != noErr || fd == NULL) return r;

    // Convert the picture NAL units to 4-byte length-prefixed form
    size_t total = 0;
    for (NSData* p in pictures) total += 4 + p.length;
    uint8_t* buf = malloc(total);
    size_t off = 0;
    for (NSData* p in pictures) {
        uint32_t be = CFSwapInt32HostToBig((uint32_t)p.length);
        memcpy(buf + off, &be, 4);
        memcpy(buf + off + 4, p.bytes, p.length);
        off += 4 + p.length;
    }

    CMBlockBufferRef block = NULL;
    OSStatus s = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, buf, total, kCFAllocatorMalloc, NULL, 0, total, 0, &block);
    if (s != noErr) { free(buf); CFRelease(fd); r.decodeStatus = s; return r; }

    CMSampleBufferRef sample = NULL;
    size_t sampleSize = total;
    s = CMSampleBufferCreateReady(kCFAllocatorDefault, block, fd, 1, 0, NULL, 1, &sampleSize, &sample);
    CFRelease(block);
    if (s != noErr) { CFRelease(fd); r.decodeStatus = s; return r; }

    NSDictionary* spec = requireHardware
        ? @{ (__bridge NSString*)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES }
        : nil;

    VTDecompressionSessionRef session = NULL;
    r.createStatus = VTDecompressionSessionCreate(kCFAllocatorDefault, fd, (__bridge CFDictionaryRef)spec, NULL, NULL, &session);
    if (r.createStatus == noErr && session != NULL) {
        __block ProbeResult* out = &r;
        VTDecodeInfoFlags flagsOut = 0;
        r.decodeStatus = VTDecompressionSessionDecodeFrameWithOutputHandler(
            session, sample, 0, &flagsOut,
            ^(OSStatus status, VTDecodeInfoFlags infoFlags, CVImageBufferRef image, CMTime pts, CMTime dur) {
                out->outputStatus = status;
                if (status == noErr && image != NULL) {
                    out->gotImage = YES;
                    out->pixelFormat = CVPixelBufferGetPixelFormatType(image);
                    out->width = CVPixelBufferGetWidth(image);
                    out->height = CVPixelBufferGetHeight(image);
                }
            });
        VTDecompressionSessionWaitForAsynchronousFrames(session);

        CFTypeRef hw = NULL;
        if (VTSessionCopyProperty(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, kCFAllocatorDefault, &hw) == noErr && hw) {
            r.usingHardware = CFBooleanGetValue(hw);
            CFRelease(hw);
        }
        VTDecompressionSessionInvalidate(session);
        CFRelease(session);
    }

    CFRelease(sample);
    CFRelease(fd);
    return r;
}

PROBE_FN NSString* ProbeDescribe(ProbeResult r) {
    if (r.formatStatus == -2) return @"test data malformed";
    if (r.formatStatus != noErr) return [NSString stringWithFormat:@"FAIL creating format description (%d)", (int)r.formatStatus];
    if (r.createStatus != noErr) return [NSString stringWithFormat:@"FAIL creating decoder (%d)", (int)r.createStatus];
    if (r.decodeStatus != noErr) return [NSString stringWithFormat:@"FAIL submitting frame (%d)", (int)r.decodeStatus];
    if (r.outputStatus != noErr || !r.gotImage) return [NSString stringWithFormat:@"FAIL decoding frame (status %d, image %@)", (int)r.outputStatus, r.gotImage ? @"yes" : @"no"];
    return [NSString stringWithFormat:@"OK  %zux%zu %@ via %@", r.width, r.height, ProbeFourCC(r.pixelFormat), r.usingHardware ? @"HARDWARE decoder" : @"SOFTWARE decoder"];
}

PROBE_FN BOOL ProbeOK(ProbeResult r) {
    return r.formatStatus == noErr && r.createStatus == noErr && r.decodeStatus == noErr && r.outputStatus == noErr && r.gotImage;
}

PROBE_FN BOOL DecoderProbeHardwareDecodes(int videoFormat);

PROBE_FN NSString* RunDecoderProbe(void) {
    struct utsname u;
    uname(&u);
    NSMutableString* report = [NSMutableString string];
    [report appendFormat:@"=== DecoderProbe ===\nmachine: %s\nOS: %@\n", u.machine, [[NSProcessInfo processInfo] operatingSystemVersionString]];
    [report appendFormat:@"VTIsHardwareDecodeSupported: H264=%@ HEVC=%@ AV1=%@\n\n",
        VTIsHardwareDecodeSupported(kCMVideoCodecType_H264) ? @"YES" : @"NO",
        VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC) ? @"YES" : @"NO",
        VTIsHardwareDecodeSupported(kCMVideoCodecType_AV1) ? @"YES" : @"NO"];

    const ProbeCase cases[] = {
        { "H.264 4:2:0 8-bit (control)",   ProbeCodecH264, k_H264TestFrame,          sizeof(k_H264TestFrame) },
        { "HEVC Main 4:2:0 8-bit (control)", ProbeCodecHEVC, k_HEVCMainTestFrame,    sizeof(k_HEVCMainTestFrame) },
        { "HEVC Main10 4:2:0 10-bit (control)", ProbeCodecHEVC, k_HEVCMain10TestFrame, sizeof(k_HEVCMain10TestFrame) },
        { "H.264 High 4:4:4 8-bit",        ProbeCodecH264, k_h264High_444TestFrame,  sizeof(k_h264High_444TestFrame) },
        { "HEVC RExt 4:4:4 8-bit",         ProbeCodecHEVC, k_HEVCRExt8_444TestFrame, sizeof(k_HEVCRExt8_444TestFrame) },
        { "HEVC RExt 4:4:4 10-bit",        ProbeCodecHEVC, k_HEVCRExt10_444TestFrame, sizeof(k_HEVCRExt10_444TestFrame) },
    };
    enum { count = sizeof(cases) / sizeof(cases[0]) };
    ProbeResult hw[count], any[count];

    for (int i = 0; i < count; i++) {
        hw[i] = ProbeDecode(&cases[i], YES);
        any[i] = ProbeDecode(&cases[i], NO);
        [report appendFormat:@"%s\n  hardware required: %@\n  any decoder:       %@\n\n", cases[i].name, ProbeDescribe(hw[i]), ProbeDescribe(any[i])];
    }

    // Controls are cases 0-2 (4:2:0). If the H.264 or HEVC Main control cannot decode, the
    // test itself is unreliable and the 4:4:4 result means nothing.
    BOOL controlsOK = ProbeOK(any[0]) && ProbeOK(any[1]);
    BOOL hw444 = ProbeOK(hw[4]);
    BOOL any444 = ProbeOK(any[3]) || ProbeOK(any[4]) || ProbeOK(any[5]);

    [report appendString:@"--- VERDICT ---\n"];
    if (!controlsOK) {
        [report appendString:@"INCONCLUSIVE: the 4:2:0 control streams did not decode, so this test cannot judge 4:4:4.\n"];
    } else if (hw444) {
        [report appendString:@"4:4:4: YES, HEVC 4:4:4 8-bit decodes on a hardware decoder.\n"];
    } else if (any444) {
        [report appendString:@"4:4:4: SOFTWARE ONLY, VideoToolbox decodes some 4:4:4 in software, not hardware. Not usable for real-time streaming.\n"];
    } else {
        [report appendString:@"4:4:4: NO, VideoToolbox on this device cannot decode H.264 or HEVC 4:4:4 (controls decoded fine).\n"];
    }
    [report appendFormat:@"YUV 4:4:4 stream gate (DecoderProbeHardwareDecodes): H.264=%@ HEVC 8-bit=%@ HEVC 10-bit=%@\n",
        DecoderProbeHardwareDecodes(VIDEO_FORMAT_H264_HIGH8_444) ? @"YES" : @"NO",
        DecoderProbeHardwareDecodes(VIDEO_FORMAT_H265_REXT8_444) ? @"YES" : @"NO",
        DecoderProbeHardwareDecodes(VIDEO_FORMAT_H265_REXT10_444) ? @"YES" : @"NO"];
    [report appendString:@"=== end ===\n"];
    return report;
}

// Whether this device decodes the given YUV 4:4:4 format on a hardware decoder. It decodes a
// real test keyframe once per format, like moonlight-qt's decoder check before it offers
// YUV 4:4:4. Unknown formats return NO.
PROBE_FN BOOL DecoderProbeHardwareDecodes(int videoFormat) {
    static NSMutableDictionary<NSNumber*, NSNumber*>* cache;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        cache = [NSMutableDictionary dictionary];
    });

    @synchronized (cache) {
        NSNumber* known = cache[@(videoFormat)];
        if (known != nil) {
            return known.boolValue;
        }

        ProbeCase c;
        switch (videoFormat) {
            case VIDEO_FORMAT_H264_HIGH8_444:
                c = (ProbeCase){ "H.264 High 4:4:4 8-bit", ProbeCodecH264, k_h264High_444TestFrame, sizeof(k_h264High_444TestFrame) };
                break;
            case VIDEO_FORMAT_H265_REXT8_444:
                c = (ProbeCase){ "HEVC RExt 4:4:4 8-bit", ProbeCodecHEVC, k_HEVCRExt8_444TestFrame, sizeof(k_HEVCRExt8_444TestFrame) };
                break;
            case VIDEO_FORMAT_H265_REXT10_444:
                c = (ProbeCase){ "HEVC RExt 4:4:4 10-bit", ProbeCodecHEVC, k_HEVCRExt10_444TestFrame, sizeof(k_HEVCRExt10_444TestFrame) };
                break;
            default:
                return NO;
        }

        ProbeResult r = ProbeDecode(&c, YES);
        BOOL supported = ProbeOK(r) && r.usingHardware;
        cache[@(videoFormat)] = @(supported);
        return supported;
    }
}
