/*
 * Copyright (C) 2019 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "MediaConfiguration.h"
#include "MediaPlayerEnums.h"
#include "RTCRtpCapabilities.h"
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/Noncopyable.h>
#include <wtf/OptionSet.h>
#include <wtf/text/AtomString.h>
#include <wtf/text/AtomStringHash.h>
#include <wtf/text/StringHash.h>

#if USE(GSTREAMER_WEBRTC)
#include <gst/rtp/rtp.h>
#endif

namespace WebCore {
class ContentType;

void teardownGStreamerRegistryScanner();

class GStreamerRegistryScanner {
    WTF_MAKE_NONCOPYABLE(GStreamerRegistryScanner)
public:
    static bool singletonWasInitialized();
    static GStreamerRegistryScanner& singleton();
    static void getSupportedDecodingTypes(HashSet<String>&);

    explicit GStreamerRegistryScanner(bool isMediaSource = false);
    ~GStreamerRegistryScanner() = default;

    void refresh();
    void teardown();

    enum Configuration {
        Decoding = 0,
        Encoding
    };

    const HashSet<String>& mimeTypeSet(Configuration) const;
    bool isContainerTypeSupported(Configuration, const String& containerType) const;

    struct RegistryLookupResult {
        bool isSupported { false };
        bool isUsingHardware { false };
        GRefPtr<GstElementFactory> factory;

        operator bool() const { return isSupported; }

        static RegistryLookupResult merge(const RegistryLookupResult& a, const RegistryLookupResult& b)
        {
            return RegistryLookupResult {
                a.isSupported && b.isSupported,
                a.isSupported && b.isSupported && a.isUsingHardware && b.isUsingHardware,
                nullptr
            };
        }

        friend bool operator==(const RegistryLookupResult& lhs, const RegistryLookupResult& rhs)
        {
            return lhs.isSupported == rhs.isSupported && lhs.isUsingHardware == rhs.isUsingHardware;
        }
    };
    RegistryLookupResult isDecodingSupported(MediaConfiguration& mediaConfiguration) const { return isConfigurationSupported(Configuration::Decoding, mediaConfiguration); };
    RegistryLookupResult isEncodingSupported(MediaConfiguration& mediaConfiguration) const { return isConfigurationSupported(Configuration::Encoding, mediaConfiguration); }

    struct CodecLookupResult {
        CodecLookupResult() = default;
        CodecLookupResult(bool isSupported, const GRefPtr<GstElementFactory>& factory)
            : isSupported(isSupported)
            , factory(factory)
        {
        }

        operator bool() const { return isSupported; }

        bool isSupported { false };
        GRefPtr<GstElementFactory> factory;
    };

    enum class CaseSensitiveCodecName : bool { No, Yes };
    CodecLookupResult isCodecSupported(Configuration, const String& codec, bool usingHardware = false, CaseSensitiveCodecName = CaseSensitiveCodecName::Yes) const;
    MediaPlayerEnums::SupportsType isContentTypeSupported(Configuration, const ContentType&, const Vector<ContentType>& contentTypesRequiringHardwareSupport, CaseSensitiveCodecName = CaseSensitiveCodecName::Yes) const;
    bool areAllCodecsSupported(Configuration, const Vector<String>& codecs, bool shouldCheckForHardwareUse = false) const;

    CodecLookupResult areCapsSupported(Configuration, const GRefPtr<GstCaps>&, bool shouldCheckForHardwareUse) const;

#if USE(GSTREAMER_WEBRTC)
    RTCRtpCapabilities audioRtpCapabilities(Configuration);
    RTCRtpCapabilities videoRtpCapabilities(Configuration);
    Vector<RTCRtpCapabilities::HeaderExtensionCapability> audioRtpExtensions();
    Vector<RTCRtpCapabilities::HeaderExtensionCapability> videoRtpExtensions();
    RegistryLookupResult isRtpPacketizerSupported(const String& encoding);
    bool isRtpHeaderExtensionSupported(StringView);
#endif

protected:
    struct ElementFactories {
        enum class Type {
            AudioParser  = 1 << 0,
            AudioDecoder = 1 << 1,
            VideoParser  = 1 << 2,
            VideoDecoder = 1 << 3,
            Demuxer      = 1 << 4,
            AudioEncoder = 1 << 5,
            VideoEncoder = 1 << 6,
            Muxer        = 1 << 7,
            RtpPayloader = 1 << 8,
            RtpDepayloader = 1 << 9,
            Decryptor    = 1 << 10,
            All          = (1 << 10) - 1
        };

        explicit ElementFactories(OptionSet<Type>);
        ~ElementFactories();

        static const char* elementFactoryTypeToString(Type);
        GList* factory(Type) const;

        enum class CheckHardwareClassifier : bool { No, Yes };
        RegistryLookupResult hasElementForMediaType(Type, const ASCIILiteral& capsString, CheckHardwareClassifier = CheckHardwareClassifier::No, std::optional<Vector<String>> disallowedList = std::nullopt) const;
        RegistryLookupResult hasElementForCaps(Type, const GRefPtr<GstCaps>&, CheckHardwareClassifier = CheckHardwareClassifier::No, std::optional<Vector<String>> disallowedList = std::nullopt) const;

        GList* audioDecoderFactories { nullptr };
        GList* audioParserFactories { nullptr };
        GList* videoDecoderFactories { nullptr };
        GList* videoParserFactories { nullptr };
        GList* demuxerFactories { nullptr };
        GList* audioEncoderFactories { nullptr };
        GList* videoEncoderFactories { nullptr };
        GList* muxerFactories { nullptr };
        GList* rtpPayloaderFactories { nullptr };
        GList* rtpDepayloaderFactories { nullptr };
        GList* decryptorFactories { nullptr };
    };

    void initializeDecoders(const ElementFactories&);
    void initializeEncoders(const ElementFactories&);

    RegistryLookupResult isConfigurationSupported(Configuration, const MediaConfiguration&) const;

    struct GstCapsWebKitMapping {
        ElementFactories::Type elementType;
        ASCIILiteral capsString;
        Vector<ASCIILiteral> webkitMIMETypes;
        Vector<ASCIILiteral> webkitCodecPatterns;
    };
    void fillMimeTypeSetFromCapsMapping(const ElementFactories&, const Vector<GstCapsWebKitMapping>&);

private:
    CodecLookupResult isAVC1CodecSupported(Configuration, const String& codec, bool shouldCheckForHardwareUse) const;
    CodecLookupResult isHEVCCodecSupported(Configuration, const String& codec, bool shouldCheckForHardwareUse) const;

    ASCIILiteral configurationNameForLogging(Configuration) const;
    bool supportsFeatures(const String& features) const;

#if USE(GSTREAMER_WEBRTC)
    void fillAudioRtpCapabilities(Configuration, RTCRtpCapabilities&);
    void fillVideoRtpCapabilities(Configuration, RTCRtpCapabilities&);

#define WEBRTC_EXPERIMENTS_HDREXT "http://www.webrtc.org/experiments/rtp-hdrext/"
    Vector<ASCIILiteral> m_commonRtpExtensions {
        "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"_s,
        WEBRTC_EXPERIMENTS_HDREXT "abs-send-time"_s,
        GST_RTP_HDREXT_BASE "sdes:mid"_s,
        GST_RTP_HDREXT_BASE "sdes:repaired-rtp-stream-id"_s,
        GST_RTP_HDREXT_BASE "sdes:rtp-stream-id"_s,
        GST_RTP_HDREXT_BASE "toffset"_s,
        GST_RTP_HDREXT_BASE "ntp-64"_s,
    };
    Vector<ASCIILiteral> m_allAudioRtpExtensions {
        GST_RTP_HDREXT_BASE "ssrc-audio-level"_s
    };
    Vector<ASCIILiteral> m_allVideoRtpExtensions {
        WEBRTC_EXPERIMENTS_HDREXT "color-space"_s,
        WEBRTC_EXPERIMENTS_HDREXT "playout-delay"_s,
        WEBRTC_EXPERIMENTS_HDREXT "video-content-type"_s,
        WEBRTC_EXPERIMENTS_HDREXT "video-timing"_s,
        "urn:3gpp:video-orientation"_s
    };
#undef WEBRTC_EXPERIMENTS_HDREXT

    std::optional<Vector<RTCRtpCapabilities::HeaderExtensionCapability>> m_audioRtpExtensions;
    std::optional<Vector<RTCRtpCapabilities::HeaderExtensionCapability>> m_videoRtpExtensions;
#endif

    bool m_isMediaSource { false };
    HashSet<String> m_decoderMimeTypeSet;
    HashMap<String, RegistryLookupResult> m_decoderCodecMap;
    HashSet<String> m_encoderMimeTypeSet;
    HashMap<String, RegistryLookupResult> m_encoderCodecMap;
};

} // namespace WebCore

#endif // USE(GSTREAMER)
