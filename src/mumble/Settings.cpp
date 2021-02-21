// Copyright 2005-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Settings.h"

#include "AudioInput.h"
#include "Cert.h"
#include "Log.h"
#include "SSL.h"

#include "../../overlay/overlay.h"

#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtGui/QImageReader>
#include <QtWidgets/QSystemTrayIcon>
#if QT_VERSION >= QT_VERSION_CHECK(5,9,0)
#	include <QOperatingSystemVersion>
#endif

#include <boost/typeof/typeof.hpp>

#include <limits>

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name
// (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"



const QPoint Settings::UNSPECIFIED_POSITION =
	QPoint(std::numeric_limits< int >::min(), std::numeric_limits< int >::max());

bool Shortcut::isServerSpecific() const {
	if (qvData.canConvert< ShortcutTarget >()) {
		const ShortcutTarget &sc = qvariant_cast< ShortcutTarget >(qvData);
		return sc.isServerSpecific();
	}
	return false;
}

bool Shortcut::operator<(const Shortcut &other) const {
	return (iIndex < other.iIndex);
}

bool Shortcut::operator==(const Shortcut &other) const {
	return (iIndex == other.iIndex) && (qlButtons == other.qlButtons) && (qvData == other.qvData)
		   && (bSuppress == other.bSuppress);
}

ShortcutTarget::ShortcutTarget() {
	bUsers            = true;
	bCurrentSelection = false;
	iChannel          = -3;
	bLinks = bChildren = bForceCenter = false;
}

bool ShortcutTarget::isServerSpecific() const {
	return !bCurrentSelection && !bUsers && iChannel >= 0;
}

bool ShortcutTarget::operator==(const ShortcutTarget &o) const {
	if ((bUsers != o.bUsers) || (bForceCenter != o.bForceCenter) || (bCurrentSelection != o.bCurrentSelection))
		return false;
	if (bUsers)
		return (qlUsers == o.qlUsers) && (qlSessions == o.qlSessions);
	else
		return (iChannel == o.iChannel) && (bLinks == o.bLinks) && (bChildren == o.bChildren) && (qsGroup == o.qsGroup);
}

quint32 qHash(const ShortcutTarget &t) {
	quint32 h = t.bForceCenter ? 0x55555555 : 0xaaaaaaaa;

	if (t.bCurrentSelection) {
		h ^= 0x20000000;
	}

	if (t.bUsers) {
		foreach (unsigned int u, t.qlSessions)
			h ^= u;
	} else {
		h ^= t.iChannel;
		if (t.bLinks)
			h ^= 0x80000000;
		if (t.bChildren)
			h ^= 0x40000000;
		h ^= qHash(t.qsGroup);
		h = ~h;
	}
	return h;
}

quint32 qHash(const QList< ShortcutTarget > &l) {
	quint32 h = l.count();
	foreach (const ShortcutTarget &st, l)
		h ^= qHash(st);
	return h;
}

QDataStream &operator<<(QDataStream &qds, const ShortcutTarget &st) {
	// Start by the version of this setting. This is needed to make sure we can stay compatible
	// with older versions (aka don't break existing shortcuts when updating the implementation)
	qds << QString::fromLatin1("v2");

	qds << st.bCurrentSelection << st.bUsers << st.bForceCenter;

	if (st.bCurrentSelection) {
		return qds << st.bLinks << st.bChildren;
	} else if (st.bUsers) {
		return qds << st.qlUsers;
	} else {
		return qds << st.iChannel << st.qsGroup << st.bLinks << st.bChildren;
	}
}

QDataStream &operator>>(QDataStream &qds, ShortcutTarget &st) {
	// Check for presence of a leading version string
	QString versionString;
	QIODevice *device = qds.device();

	if (device) {
		// Qt's way of serializing the stream requires us to read a few characters into
		// the stream in order to get accross some leading zeros and other meta stuff.
		char buf[16];

		// Init buf
		for (unsigned int i = 0; i < sizeof(buf); i++) {
			buf[i] = 0;
		}

		int read = device->peek(buf, sizeof(buf));

		for (int i = 0; i < read; i++) {
			if (buf[i] >= 31) {
				if (buf[i] == 'v') {
					qds >> versionString;
				} else {
					break;
				}
			}
		}
	} else {
		qCritical("Settings: Unable to determine version of setting for ShortcutTarget");
	}

	if (versionString == QLatin1String("v2")) {
		qds >> st.bCurrentSelection;
	}

	qds >> st.bUsers >> st.bForceCenter;

	if (st.bCurrentSelection) {
		return qds >> st.bLinks >> st.bChildren;
	} else if (st.bUsers) {
		return qds >> st.qlUsers;
	} else {
		return qds >> st.iChannel >> st.qsGroup >> st.bLinks >> st.bChildren;
	}
}

const QString Settings::cqsDefaultPushClickOn  = QLatin1String(":/on.ogg");
const QString Settings::cqsDefaultPushClickOff = QLatin1String(":/off.ogg");

OverlaySettings::OverlaySettings() {
	bEnable = false;

	fX    = 1.0f;
	fY    = 0.0f;
	fZoom = 0.875f;

#ifdef Q_OS_MACOS
	qsStyle = QLatin1String("Cleanlooks");
#endif

	osShow       = LinkedChannels;
	bAlwaysSelf  = true;
	uiActiveTime = 5;
	osSort       = Alphabetical;

	qcUserName[Settings::Passive]      = QColor(170, 170, 170);
	qcUserName[Settings::MutedTalking] = QColor(170, 170, 170);
	qcUserName[Settings::Talking]      = QColor(255, 255, 255);
	qcUserName[Settings::Whispering]   = QColor(128, 255, 128);
	qcUserName[Settings::Shouting]     = QColor(255, 128, 255);
	qcChannel                          = QColor(255, 255, 128);
	qcBoxPen                           = QColor(0, 0, 0, 224);
	qcBoxFill                          = QColor(0, 0, 0);

	setPreset();

	// FPS and Time display settings
	qcFps   = Qt::white;
	fFps    = 0.75f;
	qfFps   = qfUserName;
	qrfFps  = QRectF(0.0f, 0.05, -1, 0.023438f);
	bFps    = false;
	qrfTime = QRectF(0.0f, 0.0, -1, 0.023438f);
	bTime   = false;

	oemOverlayExcludeMode = OverlaySettings::LauncherFilterExclusionMode;
}

void OverlaySettings::setPreset(const OverlayPresets preset) {
	switch (preset) {
		case LargeSquareAvatar:
			uiColumns      = 2;
			fUserName      = 0.75f;
			fChannel       = 0.75f;
			fMutedDeafened = 0.5f;
			fAvatar        = 1.0f;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
			qfUserName = QFont(QLatin1String("Verdana"), 20);
#else
			qfUserName = QFont(QLatin1String("Arial"), 20);
#endif
			qfChannel = qfUserName;

			fUser[Settings::Passive]      = 0.5f;
			fUser[Settings::MutedTalking] = 0.5f;
			fUser[Settings::Talking]      = (7.0f / 8.0f);
			fUser[Settings::Whispering]   = (7.0f / 8.0f);
			fUser[Settings::Shouting]     = (7.0f / 8.0f);

			qrfUserName      = QRectF(-0.0625f, 0.101563f - 0.0625f, 0.125f, 0.023438f);
			qrfChannel       = QRectF(-0.03125f, -0.0625f, 0.09375f, 0.015625f);
			qrfMutedDeafened = QRectF(-0.0625f, -0.0625f, 0.0625f, 0.0625f);
			qrfAvatar        = QRectF(-0.0625f, -0.0625f, 0.125f, 0.125f);

			fBoxPenWidth = (1.f / 256.0f);
			fBoxPad      = (1.f / 256.0f);

			bUserName      = true;
			bChannel       = true;
			bMutedDeafened = true;
			bAvatar        = true;
			bBox           = false;

			qaUserName      = Qt::AlignCenter;
			qaMutedDeafened = Qt::AlignLeft | Qt::AlignTop;
			qaAvatar        = Qt::AlignCenter;
			qaChannel       = Qt::AlignCenter;
			break;
		case AvatarAndName:
		default:
			uiColumns      = 1;
			fUserName      = 1.0f;
			fChannel       = (7.0f / 8.0f);
			fMutedDeafened = (7.0f / 8.0f);
			fAvatar        = 1.0f;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
			qfUserName = QFont(QLatin1String("Verdana"), 20);
#else
			qfUserName = QFont(QLatin1String("Arial"), 20);
#endif
			qfChannel = qfUserName;

			fUser[Settings::Passive]      = 0.5f;
			fUser[Settings::MutedTalking] = 0.5f;
			fUser[Settings::Talking]      = (7.0f / 8.0f);
			fUser[Settings::Whispering]   = (7.0f / 8.0f);
			fUser[Settings::Shouting]     = (7.0f / 8.0f);

			qrfUserName      = QRectF(0.015625f, -0.015625f, 0.250f, 0.03125f);
			qrfChannel       = QRectF(0.03125f, -0.015625f, 0.1875f, 0.015625f);
			qrfMutedDeafened = QRectF(0.234375f, -0.015625f, 0.03125f, 0.03125f);
			qrfAvatar        = QRectF(-0.03125f, -0.015625f, 0.03125f, 0.03125f);

			fBoxPenWidth = 0.0f;
			fBoxPad      = (1.f / 256.0f);

			bUserName      = true;
			bChannel       = false;
			bMutedDeafened = true;
			bAvatar        = true;
			bBox           = true;

			qaUserName      = Qt::AlignLeft | Qt::AlignVCenter;
			qaMutedDeafened = Qt::AlignRight | Qt::AlignVCenter;
			qaAvatar        = Qt::AlignCenter;
			qaChannel       = Qt::AlignLeft | Qt::AlignTop;
			break;
	}
}

Settings::Settings() {
	qRegisterMetaType< ShortcutTarget >("ShortcutTarget");
	qRegisterMetaTypeStreamOperators< ShortcutTarget >("ShortcutTarget");
	qRegisterMetaType< QVariant >("QVariant");

	atTransmit        = VAD;
	bTransmitPosition = false;
	bMute = bDeaf                  = false;
	bTTS                           = false;
	bTTSMessageReadBack            = false;
	bTTSNoScope                    = false;
	bTTSNoAuthor                   = false;
	iTTSVolume                     = 75;
	iTTSThreshold                  = 250;
	qsTTSLanguage                  = QString();
	iQuality                       = 40000;
	fVolume                        = 1.0f;
	fOtherVolume                   = 0.5f;
	bAttenuateOthersOnTalk         = false;
	bAttenuateOthers               = false;
	bAttenuateUsersOnPrioritySpeak = false;
	bOnlyAttenuateSameOutput       = false;
	bAttenuateLoopbacks            = false;
	iMinLoudness                   = 1000;
	iVoiceHold                     = 50;
	iJitterBufferSize              = 1;
	iFramesPerPacket               = 2;
#ifdef USE_RNNOISE
	noiseCancelMode = NoiseCancelRNN;
#else
	noiseCancelMode = NoiseCancelSpeex;
#endif
	iSpeexNoiseCancelStrength = -30;
	bAllowLowDelay            = true;
	uiAudioInputChannelMask   = 0xffffffffffffffffULL;

	// Idle auto actions
	iIdleTime                   = 5 * 60;
	iaeIdleAction               = Nothing;
	bUndoIdleActionUponActivity = false;

	vsVAD   = Amplitude;
	fVADmin = 0.80f;
	fVADmax = 0.98f;

	bTxAudioCue     = false;
	qsTxAudioCueOn  = cqsDefaultPushClickOn;
	qsTxAudioCueOff = cqsDefaultPushClickOff;

	bUserTop = true;

	bWhisperFriends = false;

	uiDoublePush = 0;
	pttHold      = 0;

#ifdef NO_UPDATE_CHECK
	bUpdateCheck = false;
	bPluginCheck = false;
#else
	bUpdateCheck = true;
	bPluginCheck = true;
#endif

	qsImagePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

	ceExpand             = ChannelsWithUsers;
	ceChannelDrag        = Ask;
	ceUserDrag           = Move;
	bMinimalView         = false;
	bHideFrame           = false;
	aotbAlwaysOnTop      = OnTopNever;
	bAskOnQuit           = true;
	bEnableDeveloperMenu = false;
	bLockLayout          = false;
#ifdef Q_OS_WIN
	// Don't enable minimize to tray by default on Windows >= 7
#	if QT_VERSION >= QT_VERSION_CHECK(5,9,0)
	// Since Qt 5.9 QOperatingSystemVersion is preferred over QSysInfo::WinVersion
	bHideInTray = QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows7;
#	else
	const QSysInfo::WinVersion winVer = QSysInfo::windowsVersion();
	bHideInTray                       = (winVer < QSysInfo::WV_WINDOWS7);
#	endif
#else
	const bool isUnityDesktop =
		QProcessEnvironment::systemEnvironment().value(QLatin1String("XDG_CURRENT_DESKTOP")) == QLatin1String("Unity");
	bHideInTray = !isUnityDesktop && QSystemTrayIcon::isSystemTrayAvailable();
#endif
	bStateInTray              = true;
	bUsage                    = true;
	bShowUserCount            = false;
	bShowVolumeAdjustments    = true;
	bShowNicknamesOnly        = false;
	bChatBarUseSelection      = false;
	bFilterHidesEmptyChannels = true;
	bFilterActive             = false;

	wlWindowLayout            = LayoutClassic;
	bShowContextMenuInMenuBar = false;

	ssFilter = ShowReachable;

	iOutputDelay = 5;

	bASIOEnable = true;

	qsALSAInput  = QLatin1String("default");
	qsALSAOutput = QLatin1String("default");

	qsJackClientName  = QLatin1String("mumble");
	qsJackAudioOutput = QLatin1String("1");
	bJackStartServer  = false;
	bJackAutoConnect  = true;

#ifdef Q_OS_MACOS
	// On macOS Speex can't be used, so we default to Apple's custom echo cancellation mode
	// Note that this only seems to work with when using the built-in microphone and the built-in speakers. It
	// doesn't make it worse for other combinations of input and output devices though. Thus it should be
	// safe to enable this by default.
	echoOption = EchoCancelOptionID::APPLE_AEC;
#else
	// Everywhere else Speex works and thus we default to using that
	echoOption = EchoCancelOptionID::SPEEX_MIXED;
#endif

	bExclusiveInput  = false;
	bExclusiveOutput = false;

	iPortAudioInput  = -1; // default device
	iPortAudioOutput = -1; // default device

	bPositionalAudio     = true;
	bPositionalHeadphone = false;
	fAudioMinDistance    = 1.0f;
	fAudioMaxDistance    = 15.0f;
	fAudioMaxDistVolume  = 0.80f;
	fAudioBloom          = 0.5f;

	// OverlayPrivateWin
	iOverlayWinHelperRestartCooldownMsec = 10000;
	bOverlayWinHelperX86Enable           = true;
	bOverlayWinHelperX64Enable           = true;

	iLCDUserViewMinColWidth   = 50;
	iLCDUserViewSplitterWidth = 2;

	// PTT Button window
	bShowPTTButtonWindow = false;

	// Network settings
	bTCPCompat                     = false;
	bQoS                           = true;
	bReconnect                     = true;
	bAutoConnect                   = false;
	bDisablePublicList             = false;
	ptProxyType                    = NoProxy;
	usProxyPort                    = 0;
	iMaxInFlightTCPPings           = 4;
	bUdpForceTcpAddr               = true;
	iPingIntervalMsec              = 5000;
	iConnectionTimeoutDurationMsec = 30000;
	iMaxImageWidth                 = 1024; // Allow 1024x1024 resolution
	iMaxImageHeight                = 1024;
	bSuppressIdentity              = false;
	qsSslCiphers                   = MumbleSSL::defaultOpenSSLCipherString();
	bHideOS                        = false;

	bShowTransmitModeComboBox = false;

	// Accessibility
	bHighContrast = false;

	// Recording
	qsRecordingPath  = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	qsRecordingFile  = QLatin1String("Mumble-%date-%time-%host-%user");
	rmRecordingMode  = RecordingMixdown;
	iRecordingFormat = 0;

	// Special configuration options not exposed to UI
	bDisableCELT                = false;
	disableConnectDialogEditing = false;
	bPingServersDialogViewed    = false;

	// Config updates
	uiUpdateCounter = 0;

#if defined(AUDIO_TEST)
	lmLoopMode = Server;
#else
	lmLoopMode = None;
#endif
	dPacketLoss     = 0;
	dMaxPacketDelay = 0.0f;

	requireRestartToApply = false;

	iMaxLogBlocks       = 0;
	bLog24HourClock     = true;
	iChatMessageMargins = 3;

	qpTalkingUI_Position                = UNSPECIFIED_POSITION;
	bShowTalkingUI                      = false;
	bTalkingUI_LocalUserStaysVisible    = false;
	bTalkingUI_AbbreviateChannelNames   = true;
	bTalkingUI_AbbreviateCurrentChannel = false;
	bTalkingUI_ShowLocalListeners       = false;
	iTalkingUI_RelativeFontSize         = 100;
	iTalkingUI_SilentUserLifeTime       = 10;
	iTalkingUI_ChannelHierarchyDepth    = 1;
	iTalkingUI_MaxChannelNameLength     = 20;
	iTalkingUI_PrefixCharCount          = 3;
	iTalkingUI_PostfixCharCount         = 2;
	qsTalkingUI_ChannelSeparator        = QLatin1String("/");
	qsTalkingUI_AbbreviationReplacement = QLatin1String("...");

	manualPlugin_silentUserDisplaytime = 1;

	bShortcutEnable             = true;
	bSuppressMacEventTapWarning = false;
	bEnableEvdev                = false;
	bEnableXInput2              = true;
	bEnableGKey                 = false;
	bEnableXboxInput            = true;
	bEnableWinHooks             = true;
	bDirectInputVerboseLogging  = false;
	bEnableUIAccess             = true;

	for (int i = Log::firstMsgType; i <= Log::lastMsgType; ++i) {
		qmMessages.insert(i, Settings::LogConsole | Settings::LogBalloon | Settings::LogTTS);
		qmMessageSounds.insert(i, QString());
	}

	qmMessageSounds[Log::CriticalError]          = QLatin1String(":/Critical.ogg");
	qmMessageSounds[Log::PermissionDenied]       = QLatin1String(":/PermissionDenied.ogg");
	qmMessageSounds[Log::SelfMute]               = QLatin1String(":/SelfMutedDeafened.ogg");
	qmMessageSounds[Log::SelfUnmute]             = QLatin1String(":/SelfMutedDeafened.ogg");
	qmMessageSounds[Log::SelfDeaf]               = QLatin1String(":/SelfMutedDeafened.ogg");
	qmMessageSounds[Log::SelfUndeaf]             = QLatin1String(":/SelfMutedDeafened.ogg");
	qmMessageSounds[Log::ServerConnected]        = QLatin1String(":/ServerConnected.ogg");
	qmMessageSounds[Log::ServerDisconnected]     = QLatin1String(":/ServerDisconnected.ogg");
	qmMessageSounds[Log::TextMessage]            = QLatin1String(":/TextMessage.ogg");
	qmMessageSounds[Log::PrivateTextMessage]     = qmMessageSounds[Log::TextMessage];
	qmMessageSounds[Log::ChannelJoin]            = QLatin1String(":/UserJoinedChannel.ogg");
	qmMessageSounds[Log::ChannelLeave]           = QLatin1String(":/UserLeftChannel.ogg");
	qmMessageSounds[Log::ChannelJoinConnect]     = qmMessageSounds[Log::ChannelJoin];
	qmMessageSounds[Log::ChannelLeaveDisconnect] = qmMessageSounds[Log::ChannelLeave];
	qmMessageSounds[Log::YouMutedOther]          = QLatin1String(":/UserMutedYouOrByYou.ogg");
	qmMessageSounds[Log::YouMuted]               = QLatin1String(":/UserMutedYouOrByYou.ogg");
	qmMessageSounds[Log::YouKicked]              = QLatin1String(":/UserKickedYouOrByYou.ogg");
	qmMessageSounds[Log::Recording]              = QLatin1String(":/RecordingStateChanged.ogg");

	qmMessages[Log::DebugInfo]       = Settings::LogConsole;
	qmMessages[Log::Warning]         = Settings::LogConsole | Settings::LogBalloon;
	qmMessages[Log::Information]     = Settings::LogConsole;
	qmMessages[Log::UserJoin]        = Settings::LogConsole;
	qmMessages[Log::UserLeave]       = Settings::LogConsole;
	qmMessages[Log::UserKicked]      = Settings::LogConsole;
	qmMessages[Log::OtherSelfMute]   = Settings::LogConsole;
	qmMessages[Log::OtherMutedOther] = Settings::LogConsole;
	qmMessages[Log::UserRenamed]     = Settings::LogConsole;

	// Default theme
	themeName      = QLatin1String("Mumble");
	themeStyleName = QLatin1String("Lite");
}

bool Settings::doEcho() const {
	if (AudioInputRegistrar::qmNew) {
		AudioInputRegistrar *air = AudioInputRegistrar::qmNew->value(qsAudioInput);
		if (air) {
			if ((g.s.echoOption != EchoCancelOptionID::DISABLED)
			    && air->canEcho(g.s.echoOption, qsAudioOutput))
				return true;
		}
	}
	return false;
}

bool Settings::doPositionalAudio() const {
	return bPositionalAudio;
}

#include BOOST_TYPEOF_INCREMENT_REGISTRATION_GROUP()


BOOST_TYPEOF_REGISTER_TYPE(Qt::Alignment)
BOOST_TYPEOF_REGISTER_TYPE(Settings::AudioTransmit)
BOOST_TYPEOF_REGISTER_TYPE(Settings::VADSource)
BOOST_TYPEOF_REGISTER_TYPE(Settings::LoopMode)
BOOST_TYPEOF_REGISTER_TYPE(Settings::OverlayShow)
BOOST_TYPEOF_REGISTER_TYPE(Settings::ProxyType)
BOOST_TYPEOF_REGISTER_TYPE(Settings::ChannelExpand)
BOOST_TYPEOF_REGISTER_TYPE(Settings::ChannelDrag)
BOOST_TYPEOF_REGISTER_TYPE(Settings::ServerShow)
BOOST_TYPEOF_REGISTER_TYPE(Settings::NoiseCancel)
BOOST_TYPEOF_REGISTER_TYPE(Settings::WindowLayout)
BOOST_TYPEOF_REGISTER_TYPE(Settings::AlwaysOnTopBehaviour)
BOOST_TYPEOF_REGISTER_TYPE(Settings::RecordingMode)
BOOST_TYPEOF_REGISTER_TYPE(QString)
BOOST_TYPEOF_REGISTER_TYPE(QByteArray)
BOOST_TYPEOF_REGISTER_TYPE(QColor)
BOOST_TYPEOF_REGISTER_TYPE(QVariant)
BOOST_TYPEOF_REGISTER_TYPE(QFont)
BOOST_TYPEOF_REGISTER_TYPE(EchoCancelOptionID)
BOOST_TYPEOF_REGISTER_TEMPLATE(QList, 1)

#define SAVELOAD(var, name) var = qvariant_cast< BOOST_TYPEOF(var) >(settings_ptr->value(QLatin1String(name), var))
#define LOADENUM(var, name) \
	var = static_cast< BOOST_TYPEOF(var) >(settings_ptr->value(QLatin1String(name), var).toInt())
#define LOADFLAG(var, name) \
	var = static_cast< BOOST_TYPEOF(var) >(settings_ptr->value(QLatin1String(name), static_cast< int >(var)).toInt())
#define DEPRECATED(name) \
	do {                 \
	} while (0)

// Workaround for mumble-voip/mumble#2638.
//
// Qt previously expected to be able to write
// NUL bytes in strings in plists. This is no
// longer possible, which causes Qt to write
// incomplete stings to the preferences plist.
// These are of the form "@Variant(", and, for
// Mumble, typically happen for float values.
//
// We detect this bad value and avoid loading
// it. This causes such settings to fall back
// to their defaults, instead of being set to
// a zero value.
#ifdef Q_OS_MACOS
#	undef SAVELOAD
#	define SAVELOAD(var, name)                                                                          \
		do {                                                                                             \
			if (settings_ptr->value(QLatin1String(name)).toString() != QLatin1String("@Variant(")) {     \
				var = qvariant_cast< BOOST_TYPEOF(var) >(settings_ptr->value(QLatin1String(name), var)); \
			}                                                                                            \
		} while (0)
#endif

void OverlaySettings::load() {
	load(g.qs);
}

void OverlaySettings::load(QSettings *settings_ptr) {
	SAVELOAD(bEnable, "enable");

	LOADENUM(osShow, "show");
	SAVELOAD(bAlwaysSelf, "alwaysself");
	SAVELOAD(uiActiveTime, "activetime");
	LOADENUM(osSort, "sort");

	SAVELOAD(fX, "x");
	SAVELOAD(fY, "y");
	SAVELOAD(fZoom, "zoom");
	SAVELOAD(uiColumns, "columns");

	settings_ptr->beginReadArray(QLatin1String("states"));
	for (int i = 0; i < 4; ++i) {
		settings_ptr->setArrayIndex(i);
		SAVELOAD(qcUserName[i], "color");
		SAVELOAD(fUser[i], "opacity");
	}
	settings_ptr->endArray();

	SAVELOAD(qfUserName, "userfont");
	SAVELOAD(qfChannel, "channelfont");
	SAVELOAD(qcChannel, "channelcolor");
	SAVELOAD(qfFps, "fpsfont");
	SAVELOAD(qcFps, "fpscolor");

	SAVELOAD(fBoxPad, "padding");
	SAVELOAD(fBoxPenWidth, "penwidth");
	SAVELOAD(qcBoxPen, "pencolor");
	SAVELOAD(qcBoxFill, "fillcolor");

	SAVELOAD(bUserName, "usershow");
	SAVELOAD(bChannel, "channelshow");
	SAVELOAD(bMutedDeafened, "mutedshow");
	SAVELOAD(bAvatar, "avatarshow");
	SAVELOAD(bBox, "boxshow");
	SAVELOAD(bFps, "fpsshow");
	SAVELOAD(bTime, "timeshow");

	SAVELOAD(fUserName, "useropacity");
	SAVELOAD(fChannel, "channelopacity");
	SAVELOAD(fMutedDeafened, "mutedopacity");
	SAVELOAD(fAvatar, "avataropacity");
	SAVELOAD(fFps, "fpsopacity");

	SAVELOAD(qrfUserName, "userrect");
	SAVELOAD(qrfChannel, "channelrect");
	SAVELOAD(qrfMutedDeafened, "mutedrect");
	SAVELOAD(qrfAvatar, "avatarrect");
	SAVELOAD(qrfFps, "fpsrect");
	SAVELOAD(qrfTime, "timerect");

	LOADFLAG(qaUserName, "useralign");
	LOADFLAG(qaChannel, "channelalign");
	LOADFLAG(qaMutedDeafened, "mutedalign");
	LOADFLAG(qaAvatar, "avataralign");

	LOADENUM(oemOverlayExcludeMode, "mode");
	SAVELOAD(qslLaunchers, "launchers");
	SAVELOAD(qslLaunchersExclude, "launchersexclude");
	SAVELOAD(qslWhitelist, "whitelist");
	SAVELOAD(qslWhitelistExclude, "whitelistexclude");
	SAVELOAD(qslPaths, "paths");
	SAVELOAD(qslPathsExclude, "pathsexclude");
	SAVELOAD(qslBlacklist, "blacklist");
	SAVELOAD(qslBlacklistExclude, "blacklistexclude");
}

void Settings::load() {
	load(g.qs);
}

void Settings::load(QSettings *settings_ptr) {
	// Config updates
	SAVELOAD(uiUpdateCounter, "lastupdate");

	SAVELOAD(qsDatabaseLocation, "databaselocation");

	SAVELOAD(bMute, "audio/mute");
	SAVELOAD(bDeaf, "audio/deaf");
	LOADENUM(atTransmit, "audio/transmit");
	SAVELOAD(uiDoublePush, "audio/doublepush");
	SAVELOAD(pttHold, "audio/ptthold");
	SAVELOAD(bTxAudioCue, "audio/pushclick");
	SAVELOAD(qsTxAudioCueOn, "audio/pushclickon");
	SAVELOAD(qsTxAudioCueOff, "audio/pushclickoff");
	SAVELOAD(iQuality, "audio/quality");
	SAVELOAD(iMinLoudness, "audio/loudness");
	SAVELOAD(fVolume, "audio/volume");
	SAVELOAD(fOtherVolume, "audio/othervolume");
	SAVELOAD(bAttenuateOthers, "audio/attenuateothers");
	SAVELOAD(bAttenuateOthersOnTalk, "audio/attenuateothersontalk");
	SAVELOAD(bAttenuateUsersOnPrioritySpeak, "audio/attenuateusersonpriorityspeak");
	SAVELOAD(bOnlyAttenuateSameOutput, "audio/onlyattenuatesameoutput");
	SAVELOAD(bAttenuateLoopbacks, "audio/attenuateloopbacks");
	LOADENUM(vsVAD, "audio/vadsource");
	SAVELOAD(fVADmin, "audio/vadmin");
	SAVELOAD(fVADmax, "audio/vadmax");

	int oldNoiseSuppress = 0;
	SAVELOAD(oldNoiseSuppress, "audio/noisesupress");
	SAVELOAD(iSpeexNoiseCancelStrength, "audio/speexNoiseCancelStrength");

	// Select the most negative of the 2 (one is expected to be zero as it is
	// unset). This is for compatibility as we have renamed the setting at some point.
	iSpeexNoiseCancelStrength = std::min(oldNoiseSuppress, iSpeexNoiseCancelStrength);

	LOADENUM(noiseCancelMode, "audio/noiseCancelMode");

#ifndef USE_RNNOISE
	if (noiseCancelMode == NoiseCancelRNN || noiseCancelMode == NoiseCancelBoth) {
		// Use Speex instead as this Mumble build was built without support for RNNoise
		noiseCancelMode = NoiseCancelSpeex;
	}
#endif

	SAVELOAD(bAllowLowDelay, "audio/allowlowdelay");
	SAVELOAD(uiAudioInputChannelMask, "audio/inputchannelmask");
	SAVELOAD(iVoiceHold, "audio/voicehold");
	SAVELOAD(iOutputDelay, "audio/outputdelay");

	// Idle auto actions
	SAVELOAD(iIdleTime, "audio/idletime");
	LOADENUM(iaeIdleAction, "audio/idleaction");
	SAVELOAD(bUndoIdleActionUponActivity, "audio/undoidleactionuponactivity");

	SAVELOAD(fAudioMinDistance, "audio/mindistance");
	SAVELOAD(fAudioMaxDistance, "audio/maxdistance");
	SAVELOAD(fAudioMaxDistVolume, "audio/maxdistancevolume");
	SAVELOAD(fAudioBloom, "audio/bloom");
	DEPRECATED("audio/echo");
	DEPRECATED("audio/echomulti");
	SAVELOAD(bExclusiveInput, "audio/exclusiveinput");
	SAVELOAD(bExclusiveOutput, "audio/exclusiveoutput");
	SAVELOAD(bPositionalAudio, "audio/positional");
	SAVELOAD(bPositionalHeadphone, "audio/headphone");
	SAVELOAD(qsAudioInput, "audio/input");
	SAVELOAD(qsAudioOutput, "audio/output");
	SAVELOAD(bWhisperFriends, "audio/whisperfriends");
	SAVELOAD(bTransmitPosition, "audio/postransmit");

	if (settings_ptr->contains("audio/echooptionid")) {
		// Load the new echo cancel option instead
		LOADFLAG(echoOption, "audio/echooptionid");
	} else {
#ifndef Q_OS_MACOS
		// Compatibility layer for overtaking the old (now deprecated) settings
		// This block should only be called once at the first start of the new Mumble version
		// As echo cancellation was not available on macOS before, we don't have to run this compatibility
		// code on macOS (instead simply use the new default as set in the constructor).
		if (settings_ptr->contains("audio/echo")) {
			bool deprecatedEcho      = false;
			bool deprecatedEchoMulti = false;

			SAVELOAD(deprecatedEcho, "audio/echo");
			SAVELOAD(deprecatedEchoMulti, "audio/echomulti");

			if (deprecatedEcho) {
				if (deprecatedEchoMulti) {
					echoOption = EchoCancelOptionID::SPEEX_MULTICHANNEL;
				} else {
					echoOption = EchoCancelOptionID::SPEEX_MIXED;
				}
			} else {
				echoOption = EchoCancelOptionID::DISABLED;
			}
		}
#endif
	}

	SAVELOAD(iJitterBufferSize, "net/jitterbuffer");
	SAVELOAD(iFramesPerPacket, "net/framesperpacket");

	SAVELOAD(bASIOEnable, "asio/enable");
	SAVELOAD(qsASIOclass, "asio/class");
	SAVELOAD(qlASIOmic, "asio/mic");
	SAVELOAD(qlASIOspeaker, "asio/speaker");

	SAVELOAD(qsWASAPIInput, "wasapi/input");
	SAVELOAD(qsWASAPIOutput, "wasapi/output");
	SAVELOAD(qsWASAPIRole, "wasapi/role");

	SAVELOAD(qsALSAInput, "alsa/input");
	SAVELOAD(qsALSAOutput, "alsa/output");

	SAVELOAD(qsPulseAudioInput, "pulseaudio/input");
	SAVELOAD(qsPulseAudioOutput, "pulseaudio/output");

	SAVELOAD(qsJackAudioOutput, "jack/output");
	SAVELOAD(bJackStartServer, "jack/startserver");
	SAVELOAD(bJackAutoConnect, "jack/autoconnect");

	SAVELOAD(qsOSSInput, "oss/input");
	SAVELOAD(qsOSSOutput, "oss/output");

	SAVELOAD(qsCoreAudioInput, "coreaudio/input");
	SAVELOAD(qsCoreAudioOutput, "coreaudio/output");

	SAVELOAD(iPortAudioInput, "portaudio/input");
	SAVELOAD(iPortAudioOutput, "portaudio/output");

	SAVELOAD(bTTS, "tts/enable");
	SAVELOAD(iTTSVolume, "tts/volume");
	SAVELOAD(iTTSThreshold, "tts/threshold");
	SAVELOAD(bTTSMessageReadBack, "tts/readback");
	SAVELOAD(bTTSNoScope, "tts/noscope");
	SAVELOAD(bTTSNoAuthor, "tts/noauthor");
	SAVELOAD(qsTTSLanguage, "tts/language");

	// Network settings
	SAVELOAD(bTCPCompat, "net/tcponly");
	SAVELOAD(bQoS, "net/qos");
	SAVELOAD(bReconnect, "net/reconnect");
	SAVELOAD(bAutoConnect, "net/autoconnect");
	SAVELOAD(bSuppressIdentity, "net/suppress");
	LOADENUM(ptProxyType, "net/proxytype");
	SAVELOAD(qsProxyHost, "net/proxyhost");
	SAVELOAD(usProxyPort, "net/proxyport");
	SAVELOAD(qsProxyUsername, "net/proxyusername");
	SAVELOAD(qsProxyPassword, "net/proxypassword");
	DEPRECATED("net/maximagesize");
	SAVELOAD(iMaxImageWidth, "net/maximagewidth");
	SAVELOAD(iMaxImageHeight, "net/maximageheight");
	SAVELOAD(qsServicePrefix, "net/serviceprefix");
	SAVELOAD(iMaxInFlightTCPPings, "net/maxinflighttcppings");
	SAVELOAD(iPingIntervalMsec, "net/pingintervalmsec");
	SAVELOAD(iConnectionTimeoutDurationMsec, "net/connectiontimeoutdurationmsec");
	SAVELOAD(bUdpForceTcpAddr, "net/udpforcetcpaddr");

	// Network settings - SSL
	SAVELOAD(qsSslCiphers, "net/sslciphers");

	// Privacy settings
	SAVELOAD(bHideOS, "privacy/hideos");

	SAVELOAD(qsLanguage, "ui/language");
	SAVELOAD(themeName, "ui/theme");
	SAVELOAD(themeStyleName, "ui/themestyle");
	LOADENUM(ceExpand, "ui/expand");
	LOADENUM(ceChannelDrag, "ui/drag");
	LOADENUM(ceUserDrag, "ui/userdrag");
	LOADENUM(aotbAlwaysOnTop, "ui/alwaysontop");
	SAVELOAD(bAskOnQuit, "ui/askonquit");
	SAVELOAD(bEnableDeveloperMenu, "ui/developermenu");
	SAVELOAD(bLockLayout, "ui/locklayout");
	SAVELOAD(bMinimalView, "ui/minimalview");
	SAVELOAD(bHideFrame, "ui/hideframe");
	SAVELOAD(bUserTop, "ui/usertop");
	SAVELOAD(qbaMainWindowGeometry, "ui/geometry");
	SAVELOAD(qbaMainWindowState, "ui/state");
	SAVELOAD(qbaMinimalViewGeometry, "ui/minimalviewgeometry");
	SAVELOAD(qbaMinimalViewState, "ui/minimalviewstate");
	SAVELOAD(qbaConfigGeometry, "ui/ConfigGeometry");
	LOADENUM(wlWindowLayout, "ui/WindowLayout");
	SAVELOAD(qbaSplitterState, "ui/splitter");
	SAVELOAD(qbaHeaderState, "ui/header");
	SAVELOAD(qsUsername, "ui/username");
	SAVELOAD(qsLastServer, "ui/server");
	LOADENUM(ssFilter, "ui/serverfilter");

	SAVELOAD(bUpdateCheck, "ui/updatecheck");
	SAVELOAD(bPluginCheck, "ui/plugincheck");

	SAVELOAD(bHideInTray, "ui/hidetray");
	SAVELOAD(bStateInTray, "ui/stateintray");
	SAVELOAD(bUsage, "ui/usage");
	SAVELOAD(bShowUserCount, "ui/showusercount");
	SAVELOAD(bShowVolumeAdjustments, "ui/showVolumeAdjustments");
	SAVELOAD(bShowNicknamesOnly, "ui/showNicknamesOnly");
	SAVELOAD(bChatBarUseSelection, "ui/chatbaruseselection");
	SAVELOAD(bFilterHidesEmptyChannels, "ui/filterhidesemptychannels");
	SAVELOAD(bFilterActive, "ui/filteractive");
	SAVELOAD(qsImagePath, "ui/imagepath");
	SAVELOAD(bShowContextMenuInMenuBar, "ui/showcontextmenuinmenubar");
	SAVELOAD(qbaConnectDialogGeometry, "ui/connect/geometry");
	SAVELOAD(qbaConnectDialogHeader, "ui/connect/header");
	SAVELOAD(bShowTransmitModeComboBox, "ui/transmitmodecombobox");
	SAVELOAD(bHighContrast, "ui/HighContrast");
	SAVELOAD(iMaxLogBlocks, "ui/MaxLogBlocks");
	SAVELOAD(bLog24HourClock, "ui/24HourClock");
	SAVELOAD(iChatMessageMargins, "ui/ChatMessageMargins");
	SAVELOAD(bDisablePublicList, "ui/disablepubliclist");

	// TalkingUI
	SAVELOAD(qpTalkingUI_Position, "ui/talkingUIPosition");
	SAVELOAD(bShowTalkingUI, "ui/showTalkingUI");
	SAVELOAD(bTalkingUI_LocalUserStaysVisible, "ui/talkingUI_LocalUserStaysVisible");
	SAVELOAD(bTalkingUI_AbbreviateChannelNames, "ui/talkingUI_AbbreviateChannelNames");
	SAVELOAD(bTalkingUI_AbbreviateCurrentChannel, "ui/talkingUI_AbbreviateCurrentChannel");
	SAVELOAD(bTalkingUI_ShowLocalListeners, "ui/talkingUI_ShowLocalListeners");
	SAVELOAD(iTalkingUI_RelativeFontSize, "ui/talkingUI_RelativeFontSize");
	SAVELOAD(iTalkingUI_SilentUserLifeTime, "ui/talkingUI_SilentUserLifeTime");
	SAVELOAD(iTalkingUI_ChannelHierarchyDepth, "ui/talkingUI_ChannelHierarchieDepth");
	SAVELOAD(iTalkingUI_MaxChannelNameLength, "ui/talkingUI_MaxChannelNameLength");
	SAVELOAD(iTalkingUI_PrefixCharCount, "ui/talkingUI_PrefixCharCount");
	SAVELOAD(iTalkingUI_PostfixCharCount, "ui/talkingUI_PostfixCharCount");
	SAVELOAD(qsTalkingUI_ChannelSeparator, "ui/talkingUI_ChannelSeparator");
	SAVELOAD(qsTalkingUI_AbbreviationReplacement, "ui/talkingUI_AbbreviationReplacement");

	SAVELOAD(manualPlugin_silentUserDisplaytime, "ui/manualPlugin_silentUserDisplaytime");

	// PTT Button window
	SAVELOAD(bShowPTTButtonWindow, "ui/showpttbuttonwindow");
	SAVELOAD(qbaPTTButtonWindowGeometry, "ui/pttbuttonwindowgeometry");

	// Recording
	SAVELOAD(qsRecordingPath, "recording/path");
	SAVELOAD(qsRecordingFile, "recording/file");
	LOADENUM(rmRecordingMode, "recording/mode");
	SAVELOAD(iRecordingFormat, "recording/format");

	// Special configuration options not exposed to UI
	SAVELOAD(bDisableCELT, "audio/disablecelt");
	SAVELOAD(disableConnectDialogEditing, "ui/disableconnectdialogediting");
	SAVELOAD(bPingServersDialogViewed, "consent/pingserversdialogviewed");

	// OverlayPrivateWin
	SAVELOAD(iOverlayWinHelperRestartCooldownMsec, "overlay_win/helper/restart_cooldown_msec");
	SAVELOAD(bOverlayWinHelperX86Enable, "overlay_win/helper/x86/enable");
	SAVELOAD(bOverlayWinHelperX64Enable, "overlay_win/helper/x64/enable");

	// LCD
	SAVELOAD(iLCDUserViewMinColWidth, "lcd/userview/mincolwidth");
	SAVELOAD(iLCDUserViewSplitterWidth, "lcd/userview/splitterwidth");

	QByteArray qba = qvariant_cast< QByteArray >(settings_ptr->value(QLatin1String("net/certificate")));
	if (!qba.isEmpty())
		kpCertificate = CertWizard::importCert(qba);

	SAVELOAD(bShortcutEnable, "shortcut/enable");
	SAVELOAD(bSuppressMacEventTapWarning, "shortcut/mac/suppresswarning");
	SAVELOAD(bEnableEvdev, "shortcut/linux/evdev/enable");
	SAVELOAD(bEnableXInput2, "shortcut/x11/xinput2/enable");
	SAVELOAD(bEnableGKey, "shortcut/gkey");
	SAVELOAD(bEnableXboxInput, "shortcut/windows/xbox/enable");
	SAVELOAD(bEnableWinHooks, "winhooks");
	SAVELOAD(bDirectInputVerboseLogging, "shortcut/windows/directinput/verboselogging");
	SAVELOAD(bEnableUIAccess, "shortcut/windows/uiaccess/enable");

	int nshorts = settings_ptr->beginReadArray(QLatin1String("shortcuts"));
	for (int i = 0; i < nshorts; i++) {
		settings_ptr->setArrayIndex(i);
		Shortcut s;

		s.iIndex = -2;

		SAVELOAD(s.iIndex, "index");
		SAVELOAD(s.qlButtons, "keys");
		SAVELOAD(s.bSuppress, "suppress");
		s.qvData = settings_ptr->value(QLatin1String("data"));
		if (s.iIndex >= -1)
			qlShortcuts << s;
	}
	settings_ptr->endArray();

	settings_ptr->beginReadArray(QLatin1String("messages"));
	for (QMap< int, quint32 >::const_iterator it = qmMessages.constBegin(); it != qmMessages.constEnd(); ++it) {
		settings_ptr->setArrayIndex(it.key());
		SAVELOAD(qmMessages[it.key()], "log");
	}
	settings_ptr->endArray();

	settings_ptr->beginReadArray(QLatin1String("messagesounds"));
	for (QMap< int, QString >::const_iterator it = qmMessageSounds.constBegin(); it != qmMessageSounds.constEnd();
		 ++it) {
		settings_ptr->setArrayIndex(it.key());
		SAVELOAD(qmMessageSounds[it.key()], "logsound");
	}
	settings_ptr->endArray();

	settings_ptr->beginGroup(QLatin1String("lcd/devices"));
	foreach (const QString &d, settings_ptr->childKeys()) {
		qmLCDDevices.insert(d, settings_ptr->value(d, true).toBool());
	}
	settings_ptr->endGroup();

	settings_ptr->beginGroup(QLatin1String("audio/plugins"));
	foreach (const QString &d, settings_ptr->childKeys()) {
		qmPositionalAudioPlugins.insert(d, settings_ptr->value(d, true).toBool());
	}
	settings_ptr->endGroup();

	settings_ptr->beginGroup(QLatin1String("overlay"));
	os.load(settings_ptr);
	settings_ptr->endGroup();

	qDebug() << "Echo option loaded to" << static_cast<int>(echoOption);
}

#undef SAVELOAD
#define SAVELOAD(var, name)                               \
	if (var != def.var)                                   \
		settings_ptr->setValue(QLatin1String(name), var); \
	else                                                  \
		settings_ptr->remove(QLatin1String(name))
#define SAVEFLAG(var, name)                                                   \
	if (var != def.var)                                                       \
		settings_ptr->setValue(QLatin1String(name), static_cast< int >(var)); \
	else                                                                      \
		settings_ptr->remove(QLatin1String(name))
#undef DEPRECATED
#define DEPRECATED(name) settings_ptr->remove(QLatin1String(name))

void OverlaySettings::save() {
	save(g.qs);
}

void OverlaySettings::save(QSettings *settings_ptr) {
	OverlaySettings def;

	settings_ptr->setValue(QLatin1String("version"), QLatin1String(MUMTEXT(MUMBLE_VERSION)));
	settings_ptr->sync();

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	if (settings_ptr->format() == QSettings::IniFormat)
#endif
	{
		QFile f(settings_ptr->fileName());
		f.setPermissions(f.permissions()
						 & ~(QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther
							 | QFile::WriteOther | QFile::ExeOther));
	}

	SAVELOAD(bEnable, "enable");

	SAVELOAD(osShow, "show");
	SAVELOAD(bAlwaysSelf, "alwaysself");
	SAVELOAD(uiActiveTime, "activetime");
	SAVELOAD(osSort, "sort");
	SAVELOAD(fX, "x");
	SAVELOAD(fY, "y");
	SAVELOAD(fZoom, "zoom");
	SAVELOAD(uiColumns, "columns");

	settings_ptr->beginReadArray(QLatin1String("states"));
	for (int i = 0; i < 4; ++i) {
		settings_ptr->setArrayIndex(i);
		SAVELOAD(qcUserName[i], "color");
		SAVELOAD(fUser[i], "opacity");
	}
	settings_ptr->endArray();

	SAVELOAD(qfUserName, "userfont");
	SAVELOAD(qfChannel, "channelfont");
	SAVELOAD(qcChannel, "channelcolor");
	SAVELOAD(qfFps, "fpsfont");
	SAVELOAD(qcFps, "fpscolor");

	SAVELOAD(fBoxPad, "padding");
	SAVELOAD(fBoxPenWidth, "penwidth");
	SAVELOAD(qcBoxPen, "pencolor");
	SAVELOAD(qcBoxFill, "fillcolor");

	SAVELOAD(bUserName, "usershow");
	SAVELOAD(bChannel, "channelshow");
	SAVELOAD(bMutedDeafened, "mutedshow");
	SAVELOAD(bAvatar, "avatarshow");
	SAVELOAD(bBox, "boxshow");
	SAVELOAD(bFps, "fpsshow");
	SAVELOAD(bTime, "timeshow");

	SAVELOAD(fUserName, "useropacity");
	SAVELOAD(fChannel, "channelopacity");
	SAVELOAD(fMutedDeafened, "mutedopacity");
	SAVELOAD(fAvatar, "avataropacity");
	SAVELOAD(fFps, "fpsopacity");

	SAVELOAD(qrfUserName, "userrect");
	SAVELOAD(qrfChannel, "channelrect");
	SAVELOAD(qrfMutedDeafened, "mutedrect");
	SAVELOAD(qrfAvatar, "avatarrect");
	SAVELOAD(qrfFps, "fpsrect");
	SAVELOAD(qrfTime, "timerect");

	SAVEFLAG(qaUserName, "useralign");
	SAVEFLAG(qaChannel, "channelalign");
	SAVEFLAG(qaMutedDeafened, "mutedalign");
	SAVEFLAG(qaAvatar, "avataralign");

	SAVELOAD(oemOverlayExcludeMode, "mode");
	settings_ptr->setValue(QLatin1String("launchers"), qslLaunchers);
	settings_ptr->setValue(QLatin1String("launchersexclude"), qslLaunchersExclude);
	settings_ptr->setValue(QLatin1String("whitelist"), qslWhitelist);
	settings_ptr->setValue(QLatin1String("whitelistexclude"), qslWhitelistExclude);
	settings_ptr->setValue(QLatin1String("paths"), qslPaths);
	settings_ptr->setValue(QLatin1String("pathsexclude"), qslPathsExclude);
	settings_ptr->setValue(QLatin1String("blacklist"), qslBlacklist);
	settings_ptr->setValue(QLatin1String("blacklistexclude"), qslBlacklistExclude);
}

void Settings::save() {
	QSettings *settings_ptr = g.qs;
	Settings def;

	// Config updates
	SAVELOAD(uiUpdateCounter, "lastupdate");

	SAVELOAD(qsDatabaseLocation, "databaselocation");

	SAVELOAD(bMute, "audio/mute");
	SAVELOAD(bDeaf, "audio/deaf");
	SAVELOAD(atTransmit, "audio/transmit");
	SAVELOAD(uiDoublePush, "audio/doublepush");
	SAVELOAD(pttHold, "audio/ptthold");
	SAVELOAD(bTxAudioCue, "audio/pushclick");
	SAVELOAD(qsTxAudioCueOn, "audio/pushclickon");
	SAVELOAD(qsTxAudioCueOff, "audio/pushclickoff");
	SAVELOAD(iQuality, "audio/quality");
	SAVELOAD(iMinLoudness, "audio/loudness");
	SAVELOAD(fVolume, "audio/volume");
	SAVELOAD(fOtherVolume, "audio/othervolume");
	SAVELOAD(bAttenuateOthers, "audio/attenuateothers");
	SAVELOAD(bAttenuateOthersOnTalk, "audio/attenuateothersontalk");
	SAVELOAD(bAttenuateUsersOnPrioritySpeak, "audio/attenuateusersonpriorityspeak");
	SAVELOAD(bOnlyAttenuateSameOutput, "audio/onlyattenuatesameoutput");
	SAVELOAD(bAttenuateLoopbacks, "audio/attenuateloopbacks");
	SAVELOAD(vsVAD, "audio/vadsource");
	SAVELOAD(fVADmin, "audio/vadmin");
	SAVELOAD(fVADmax, "audio/vadmax");
	SAVELOAD(noiseCancelMode, "audio/noiseCancelMode");
	SAVELOAD(iSpeexNoiseCancelStrength, "audio/speexNoiseCancelStrength");
	SAVELOAD(bAllowLowDelay, "audio/allowlowdelay");
	SAVELOAD(uiAudioInputChannelMask, "audio/inputchannelmask");
	SAVELOAD(iVoiceHold, "audio/voicehold");
	SAVELOAD(iOutputDelay, "audio/outputdelay");

	// Idle auto actions
	SAVELOAD(iIdleTime, "audio/idletime");
	SAVELOAD(iaeIdleAction, "audio/idleaction");
	SAVELOAD(bUndoIdleActionUponActivity, "audio/undoidleactionuponactivity");

	SAVELOAD(fAudioMinDistance, "audio/mindistance");
	SAVELOAD(fAudioMaxDistance, "audio/maxdistance");
	SAVELOAD(fAudioMaxDistVolume, "audio/maxdistancevolume");
	SAVELOAD(fAudioBloom, "audio/bloom");
	DEPRECATED("audio/echo");
	DEPRECATED("audio/echomulti");
	SAVELOAD(bExclusiveInput, "audio/exclusiveinput");
	SAVELOAD(bExclusiveOutput, "audio/exclusiveoutput");
	SAVELOAD(bPositionalAudio, "audio/positional");
	SAVELOAD(bPositionalHeadphone, "audio/headphone");
	SAVELOAD(qsAudioInput, "audio/input");
	SAVELOAD(qsAudioOutput, "audio/output");
	SAVELOAD(bWhisperFriends, "audio/whisperfriends");
	SAVELOAD(bTransmitPosition, "audio/postransmit");
	SAVEFLAG(echoOption, "audio/echooptionid");

	SAVELOAD(iJitterBufferSize, "net/jitterbuffer");
	SAVELOAD(iFramesPerPacket, "net/framesperpacket");

	SAVELOAD(bASIOEnable, "asio/enable");
	SAVELOAD(qsASIOclass, "asio/class");
	SAVELOAD(qlASIOmic, "asio/mic");
	SAVELOAD(qlASIOspeaker, "asio/speaker");

	SAVELOAD(qsWASAPIInput, "wasapi/input");
	SAVELOAD(qsWASAPIOutput, "wasapi/output");
	SAVELOAD(qsWASAPIRole, "wasapi/role");

	SAVELOAD(qsALSAInput, "alsa/input");
	SAVELOAD(qsALSAOutput, "alsa/output");

	SAVELOAD(qsPulseAudioInput, "pulseaudio/input");
	SAVELOAD(qsPulseAudioOutput, "pulseaudio/output");

	SAVELOAD(qsJackAudioOutput, "jack/output");
	SAVELOAD(bJackStartServer, "jack/startserver");
	SAVELOAD(bJackAutoConnect, "jack/autoconnect");

	SAVELOAD(qsOSSInput, "oss/input");
	SAVELOAD(qsOSSOutput, "oss/output");

	SAVELOAD(qsCoreAudioInput, "coreaudio/input");
	SAVELOAD(qsCoreAudioOutput, "coreaudio/output");

	SAVELOAD(iPortAudioInput, "portaudio/input");
	SAVELOAD(iPortAudioOutput, "portaudio/output");

	SAVELOAD(bTTS, "tts/enable");
	SAVELOAD(iTTSVolume, "tts/volume");
	SAVELOAD(iTTSThreshold, "tts/threshold");
	SAVELOAD(bTTSMessageReadBack, "tts/readback");
	SAVELOAD(bTTSNoScope, "tts/noscope");
	SAVELOAD(bTTSNoAuthor, "tts/noauthor");
	SAVELOAD(qsTTSLanguage, "tts/language");

	// Network settings
	SAVELOAD(bTCPCompat, "net/tcponly");
	SAVELOAD(bQoS, "net/qos");
	SAVELOAD(bReconnect, "net/reconnect");
	SAVELOAD(bAutoConnect, "net/autoconnect");
	SAVELOAD(ptProxyType, "net/proxytype");
	SAVELOAD(qsProxyHost, "net/proxyhost");
	SAVELOAD(usProxyPort, "net/proxyport");
	SAVELOAD(qsProxyUsername, "net/proxyusername");
	SAVELOAD(qsProxyPassword, "net/proxypassword");
	DEPRECATED("net/maximagesize");
	SAVELOAD(iMaxImageWidth, "net/maximagewidth");
	SAVELOAD(iMaxImageHeight, "net/maximageheight");
	SAVELOAD(qsServicePrefix, "net/serviceprefix");
	SAVELOAD(iMaxInFlightTCPPings, "net/maxinflighttcppings");
	SAVELOAD(iPingIntervalMsec, "net/pingintervalmsec");
	SAVELOAD(iConnectionTimeoutDurationMsec, "net/connectiontimeoutdurationmsec");
	SAVELOAD(bUdpForceTcpAddr, "net/udpforcetcpaddr");

	// Network settings - SSL
	SAVELOAD(qsSslCiphers, "net/sslciphers");

	// Privacy settings
	SAVELOAD(bHideOS, "privacy/hideos");

	SAVELOAD(qsLanguage, "ui/language");
	SAVELOAD(themeName, "ui/theme");
	SAVELOAD(themeStyleName, "ui/themestyle");
	SAVELOAD(ceExpand, "ui/expand");
	SAVELOAD(ceChannelDrag, "ui/drag");
	SAVELOAD(ceUserDrag, "ui/userdrag");
	SAVELOAD(aotbAlwaysOnTop, "ui/alwaysontop");
	SAVELOAD(bAskOnQuit, "ui/askonquit");
	SAVELOAD(bEnableDeveloperMenu, "ui/developermenu");
	SAVELOAD(bLockLayout, "ui/locklayout");
	SAVELOAD(bMinimalView, "ui/minimalview");
	SAVELOAD(bHideFrame, "ui/hideframe");
	SAVELOAD(bUserTop, "ui/usertop");
	SAVELOAD(qbaMainWindowGeometry, "ui/geometry");
	SAVELOAD(qbaMainWindowState, "ui/state");
	SAVELOAD(qbaMinimalViewGeometry, "ui/minimalviewgeometry");
	SAVELOAD(qbaMinimalViewState, "ui/minimalviewstate");
	SAVELOAD(qbaConfigGeometry, "ui/ConfigGeometry");
	SAVELOAD(wlWindowLayout, "ui/WindowLayout");
	SAVELOAD(qbaSplitterState, "ui/splitter");
	SAVELOAD(qbaHeaderState, "ui/header");
	SAVELOAD(qsUsername, "ui/username");
	SAVELOAD(qsLastServer, "ui/server");
	SAVELOAD(ssFilter, "ui/serverfilter");
	SAVELOAD(bUpdateCheck, "ui/updatecheck");
	SAVELOAD(bPluginCheck, "ui/plugincheck");
	SAVELOAD(bHideInTray, "ui/hidetray");
	SAVELOAD(bStateInTray, "ui/stateintray");
	SAVELOAD(bUsage, "ui/usage");
	SAVELOAD(bShowUserCount, "ui/showusercount");
	SAVELOAD(bShowVolumeAdjustments, "ui/showVolumeAdjustments");
	SAVELOAD(bShowNicknamesOnly, "ui/showNicknamesOnly");
	SAVELOAD(bChatBarUseSelection, "ui/chatbaruseselection");
	SAVELOAD(bFilterHidesEmptyChannels, "ui/filterhidesemptychannels");
	SAVELOAD(bFilterActive, "ui/filteractive");
	SAVELOAD(qsImagePath, "ui/imagepath");
	SAVELOAD(bShowContextMenuInMenuBar, "ui/showcontextmenuinmenubar");
	SAVELOAD(qbaConnectDialogGeometry, "ui/connect/geometry");
	SAVELOAD(qbaConnectDialogHeader, "ui/connect/header");
	SAVELOAD(bShowTransmitModeComboBox, "ui/transmitmodecombobox");
	SAVELOAD(bHighContrast, "ui/HighContrast");
	SAVELOAD(iMaxLogBlocks, "ui/MaxLogBlocks");
	SAVELOAD(bLog24HourClock, "ui/24HourClock");
	SAVELOAD(iChatMessageMargins, "ui/ChatMessageMargins");
	SAVELOAD(bDisablePublicList, "ui/disablepubliclist");

	// TalkingUI
	SAVELOAD(qpTalkingUI_Position, "ui/talkingUIPosition");
	SAVELOAD(bShowTalkingUI, "ui/showTalkingUI");
	SAVELOAD(bTalkingUI_LocalUserStaysVisible, "ui/talkingUI_LocalUserStaysVisible");
	SAVELOAD(bTalkingUI_AbbreviateChannelNames, "ui/talkingUI_AbbreviateChannelNames");
	SAVELOAD(bTalkingUI_AbbreviateCurrentChannel, "ui/talkingUI_AbbreviateCurrentChannel");
	SAVELOAD(bTalkingUI_ShowLocalListeners, "ui/talkingUI_ShowLocalListeners");
	SAVELOAD(iTalkingUI_RelativeFontSize, "ui/talkingUI_RelativeFontSize");
	SAVELOAD(iTalkingUI_SilentUserLifeTime, "ui/talkingUI_SilentUserLifeTime");
	SAVELOAD(iTalkingUI_ChannelHierarchyDepth, "ui/talkingUI_ChannelHierarchieDepth");
	SAVELOAD(iTalkingUI_MaxChannelNameLength, "ui/talkingUI_MaxChannelNameLength");
	SAVELOAD(iTalkingUI_PrefixCharCount, "ui/talkingUI_PrefixCharCount");
	SAVELOAD(iTalkingUI_PostfixCharCount, "ui/talkingUI_PostfixCharCount");
	SAVELOAD(qsTalkingUI_ChannelSeparator, "ui/talkingUI_ChannelSeparator");
	SAVELOAD(qsTalkingUI_AbbreviationReplacement, "ui/talkingUI_AbbreviationReplacement");

	SAVELOAD(manualPlugin_silentUserDisplaytime, "ui/manualPlugin_silentUserDisplaytime");

	// PTT Button window
	SAVELOAD(bShowPTTButtonWindow, "ui/showpttbuttonwindow");
	SAVELOAD(qbaPTTButtonWindowGeometry, "ui/pttbuttonwindowgeometry");

	// Recording
	SAVELOAD(qsRecordingPath, "recording/path");
	SAVELOAD(qsRecordingFile, "recording/file");
	SAVELOAD(rmRecordingMode, "recording/mode");
	SAVELOAD(iRecordingFormat, "recording/format");

	// Special configuration options not exposed to UI
	SAVELOAD(bDisableCELT, "audio/disablecelt");
	SAVELOAD(disableConnectDialogEditing, "ui/disableconnectdialogediting");
	SAVELOAD(bPingServersDialogViewed, "consent/pingserversdialogviewed");

	// OverlayPrivateWin
	SAVELOAD(iOverlayWinHelperRestartCooldownMsec, "overlay_win/helper/restart_cooldown_msec");
	SAVELOAD(bOverlayWinHelperX86Enable, "overlay_win/helper/x86/enable");
	SAVELOAD(bOverlayWinHelperX64Enable, "overlay_win/helper/x64/enable");

	// LCD
	SAVELOAD(iLCDUserViewMinColWidth, "lcd/userview/mincolwidth");
	SAVELOAD(iLCDUserViewSplitterWidth, "lcd/userview/splitterwidth");

	QByteArray qba = CertWizard::exportCert(kpCertificate);
	settings_ptr->setValue(QLatin1String("net/certificate"), qba);

	SAVELOAD(bShortcutEnable, "shortcut/enable");
	SAVELOAD(bSuppressMacEventTapWarning, "shortcut/mac/suppresswarning");
	SAVELOAD(bEnableEvdev, "shortcut/linux/evdev/enable");
	SAVELOAD(bEnableXInput2, "shortcut/x11/xinput2/enable");
	SAVELOAD(bEnableGKey, "shortcut/gkey");
	SAVELOAD(bEnableXboxInput, "shortcut/windows/xbox/enable");
	SAVELOAD(bEnableWinHooks, "winhooks");
	SAVELOAD(bDirectInputVerboseLogging, "shortcut/windows/directinput/verboselogging");
	SAVELOAD(bEnableUIAccess, "shortcut/windows/uiaccess/enable");

	settings_ptr->beginWriteArray(QLatin1String("shortcuts"));
	int idx = 0;
	foreach (const Shortcut &s, qlShortcuts) {
		if (!s.isServerSpecific()) {
			settings_ptr->setArrayIndex(idx++);
			settings_ptr->setValue(QLatin1String("index"), s.iIndex);
			settings_ptr->setValue(QLatin1String("keys"), s.qlButtons);
			settings_ptr->setValue(QLatin1String("suppress"), s.bSuppress);
			settings_ptr->setValue(QLatin1String("data"), s.qvData);
		}
	}
	settings_ptr->endArray();

	settings_ptr->beginWriteArray(QLatin1String("messages"));
	for (QMap< int, quint32 >::const_iterator it = qmMessages.constBegin(); it != qmMessages.constEnd(); ++it) {
		settings_ptr->setArrayIndex(it.key());
		SAVELOAD(qmMessages[it.key()], "log");
	}
	settings_ptr->endArray();

	settings_ptr->beginWriteArray(QLatin1String("messagesounds"));
	for (QMap< int, QString >::const_iterator it = qmMessageSounds.constBegin(); it != qmMessageSounds.constEnd();
		 ++it) {
		settings_ptr->setArrayIndex(it.key());
		SAVELOAD(qmMessageSounds[it.key()], "logsound");
	}
	settings_ptr->endArray();

	settings_ptr->beginGroup(QLatin1String("lcd/devices"));
	foreach (const QString &d, qmLCDDevices.keys()) {
		bool v = qmLCDDevices.value(d);
		if (!v)
			settings_ptr->setValue(d, v);
		else
			settings_ptr->remove(d);
	}
	settings_ptr->endGroup();

	settings_ptr->beginGroup(QLatin1String("audio/plugins"));
	foreach (const QString &d, qmPositionalAudioPlugins.keys()) {
		bool v = qmPositionalAudioPlugins.value(d);
		if (!v)
			settings_ptr->setValue(d, v);
		else
			settings_ptr->remove(d);
	}
	settings_ptr->endGroup();

	settings_ptr->beginGroup(QLatin1String("overlay"));
	os.save(settings_ptr);
	settings_ptr->endGroup();
}
