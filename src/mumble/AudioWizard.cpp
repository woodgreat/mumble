// Copyright 2005-2020 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "AudioWizard.h"

#include "AudioInput.h"
#include "AudioOutputSample.h"
#include "Log.h"
#include "MainWindow.h"
#include "Utils.h"
#include "GlobalShortcut.h"
#include "GlobalShortcutButtons.h"

#include <QtGui/QMouseEvent>
#include <QtWidgets/QGraphicsEllipseItem>

#include <cmath>

// We define a global macro called 'g'. This can lead to issues when included code uses 'g' as a type or parameter name
// (like protobuf 3.7 does). As such, for now, we have to make this our last include.
#include "Global.h"

AudioWizard::AudioWizard(QWidget *p) : QWizard(p) {
	bInit            = true;
	bLastActive      = false;
	g.bInAudioWizard = true;

	ticker = new QTimer(this);
	ticker->setObjectName(QLatin1String("Ticker"));

	setupUi(this);

	qcbInput->setAccessibleName(tr("Input system"));
	qcbInputDevice->setAccessibleName(tr("Input device"));
	qcbOutput->setAccessibleName(tr("Output system"));
	qcbOutputDevice->setAccessibleName(tr("Output device"));
	qsOutputDelay->setAccessibleName(tr("Output delay"));
	qsMaxAmp->setAccessibleName(tr("Maximum amplification"));
	qsVAD->setAccessibleName(tr("VAD level"));

	// Done
	qcbUsage->setChecked(g.s.bUsage);

	// Device
	if (AudioInputRegistrar::qmNew) {
		foreach (AudioInputRegistrar *air, *AudioInputRegistrar::qmNew) {
			qcbInput->addItem(air->name);
			if (air->name == AudioInputRegistrar::current) {
				qcbInput->setCurrentIndex(qcbInput->count() - 1);
				EchoCancelOptionID echoCancelOptionId = firstUsableEchoCancellation(air, qcbOutput->currentText());
				if (echoCancelOptionId != EchoCancelOptionID::DISABLED) {
					qcbEcho->setEnabled(true);
					qcbEcho->setChecked(g.s.echoOption != EchoCancelOptionID::DISABLED);
				}
			}
			QList< audioDevice > ql = air->getDeviceChoices();
		}
	}
	if (qcbInput->count() < 2) {
		qcbInput->setEnabled(false);
	}

	if (AudioOutputRegistrar::qmNew) {
		foreach (AudioOutputRegistrar *aor, *AudioOutputRegistrar::qmNew) {
			qcbOutput->addItem(aor->name);
			if (aor->name == AudioOutputRegistrar::current) {
				qcbOutput->setCurrentIndex(qcbOutput->count() - 1);
				bDelay = aor->usesOutputDelay();
				qcbAttenuateOthers->setEnabled(aor->canMuteOthers());
			}
			QList< audioDevice > ql = aor->getDeviceChoices();
		}
	}

	if (qcbOutput->count() < 2) {
		qcbOutput->setEnabled(false);
	}

	qcbHighContrast->setChecked(g.s.bHighContrast);
	on_qcbHighContrast_clicked(g.s.bHighContrast);
#ifdef Q_OS_WIN
	// On windows we can autodetect this
	qcbHighContrast->setVisible(false);
#endif

	// Settings
	if (g.s.iQuality == 16000 && g.s.iFramesPerPacket == 6)
		qrbQualityLow->setChecked(true);
	else if (g.s.iQuality == 40000 && g.s.iFramesPerPacket == 2)
		qrbQualityBalanced->setChecked(true);
	else if (g.s.iQuality == 72000 && g.s.iFramesPerPacket == 1)
		qrbQualityUltra->setChecked(true);
	else
		qrbQualityCustom->setChecked(true);

	quint32 iMessage = Settings::LogNone;
	for (int i = Log::firstMsgType; i <= Log::lastMsgType; ++i) {
		iMessage |= (g.s.qmMessages[i] & (Settings::LogSoundfile | Settings::LogTTS));
	}

#ifdef USE_NO_TTS
	qrbNotificationCustom->setChecked(false);
	qrbNotificationCustom->setDisabled(true);
	qrbNotificationTTS->setChecked(false);
	qrbNotificationTTS->setDisabled(true);
	qrbNotificationSounds->setChecked(true);
#else
	if (iMessage == Settings::LogTTS && g.s.bTTS)
		qrbNotificationTTS->setChecked(true);
	else if (iMessage == Settings::LogSoundfile)
		qrbNotificationSounds->setChecked(true);
	else // If we find mixed message types or only tts with main tts disable assume custom
		qrbNotificationCustom->setChecked(true);
	qrbNotificationCustom->setVisible(qrbNotificationCustom->isChecked());
#endif

	qrbQualityCustom->setVisible(qrbQualityCustom->isChecked());
	qlQualityCustom->setVisible(qrbQualityCustom->isChecked());

	qcbPositional->setChecked(g.s.bPositionalAudio);
	qcbAttenuateOthers->setChecked(g.s.bAttenuateOthers);

	on_qcbInput_activated(qcbInput->currentIndex());
	on_qcbOutput_activated(qcbOutput->currentIndex());

	abAmplify->qcBelow  = Qt::blue;
	abAmplify->qcInside = Qt::green;
	abAmplify->qcAbove  = Qt::red;

	for (const auto &shortcut : g.s.qlShortcuts) {
		if (shortcut.iIndex == g.mw->gsPushTalk->idx) {
			pttButtons = shortcut.qlButtons;
			break;
		}
	}

	if (g.s.atTransmit == Settings::PushToTalk)
		qrPTT->setChecked(true);
	else if (g.s.vsVAD == Settings::Amplitude)
		qrAmplitude->setChecked(true);
	else
		qrSNR->setChecked(true);

	abVAD->qcBelow  = Qt::red;
	abVAD->qcInside = Qt::yellow;
	abVAD->qcAbove  = Qt::green;

	qsVAD->setValue(iroundf(g.s.fVADmax * 32767.f + 0.5f));

	// Positional
	qcbHeadphone->setChecked(g.s.bPositionalHeadphone);

	fAngle = 0.0f;
	fX = fY   = 0.0f;
	qgsScene  = nullptr;
	qgiSource = nullptr;
	aosSource = nullptr;
	qgvView->scale(1.0f, -1.0f);
	qgvView->viewport()->installEventFilter(this);
	qgvView->setRenderHints(QPainter::Antialiasing);

	// Volume
	qsMaxAmp->setValue(g.s.iMinLoudness);

	// Device Tuning
	qsOutputDelay->setValue(g.s.iOutputDelay);

	on_qsOutputDelay_valueChanged(qsOutputDelay->value());

	setOption(QWizard::NoCancelButton, false);
	resize(700, 500);

	updateTriggerWidgets(qrPTT->isChecked());
	sOldSettings        = g.s;
	g.s.lmLoopMode      = Settings::Local;
	g.s.dPacketLoss     = 0.0;
	g.s.dMaxPacketDelay = 0.0;
	g.s.bMute           = true;
	g.s.bDeaf           = false;

	bTransmitChanged = false;

	iMaxPeak = 0;
	iTicks   = 0;

	qpTalkingOn  = QPixmap::fromImage(QImage(QLatin1String("skin:talking_on.svg")).scaled(64, 64));
	qpTalkingOff = QPixmap::fromImage(QImage(QLatin1String("skin:talking_off.svg")).scaled(64, 64));

	bInit = false;

	connect(this, SIGNAL(currentIdChanged(int)), this, SLOT(showPage(int)));

	ticker->setSingleShot(false);
	ticker->start(20);
}

bool AudioWizard::eventFilter(QObject *obj, QEvent *evt) {
	if ((evt->type() == QEvent::MouseButtonPress) || (evt->type() == QEvent::MouseMove)) {
		QMouseEvent *qme = dynamic_cast< QMouseEvent * >(evt);
		if (qme) {
			if (qme->buttons() & Qt::LeftButton) {
				QPointF qpf = qgvView->mapToScene(qme->pos());
				fX          = static_cast< float >(qpf.x());
				fY          = static_cast< float >(qpf.y());
			}
		}
	}
	return QWizard::eventFilter(obj, evt);
}

void AudioWizard::on_qcbInput_activated(int) {
	qcbInputDevice->clear();

	if (!AudioInputRegistrar::qmNew)
		return;

	AudioInputRegistrar *air = AudioInputRegistrar::qmNew->value(qcbInput->currentText());
	QList< audioDevice > ql  = air->getDeviceChoices();

	foreach (audioDevice d, ql) { qcbInputDevice->addItem(d.first, d.second); }

	qcbInputDevice->setEnabled(ql.count() > 1);

	on_qcbInputDevice_activated(0);
}

void AudioWizard::on_qcbInputDevice_activated(int) {
	if (bInit)
		return;

	if (!AudioInputRegistrar::qmNew)
		return;

	Audio::stopInput();

	AudioInputRegistrar *air = AudioInputRegistrar::qmNew->value(qcbInput->currentText());
	int idx                  = qcbInputDevice->currentIndex();
	if (idx > -1) {
		air->setDeviceChoice(qcbInputDevice->itemData(idx), g.s);
	}

	EchoCancelOptionID echoCancelOptionId = firstUsableEchoCancellation(air, qcbOutput->currentText());
	qcbEcho->setEnabled(echoCancelOptionId != EchoCancelOptionID::DISABLED);

	g.ai = AudioInputPtr(air->create());
	g.ai->start(QThread::HighestPriority);
}

void AudioWizard::on_qcbOutput_activated(int) {
	qcbOutputDevice->clear();

	if (!AudioOutputRegistrar::qmNew)
		return;

	AudioOutputRegistrar *aor = AudioOutputRegistrar::qmNew->value(qcbOutput->currentText());
	QList< audioDevice > ql   = aor->getDeviceChoices();

	foreach (audioDevice d, ql) { qcbOutputDevice->addItem(d.first, d.second); }

	qcbAttenuateOthers->setEnabled(aor->canMuteOthers());

	qcbOutputDevice->setEnabled(ql.count() > 1);

	on_qcbOutputDevice_activated(0);
}

void AudioWizard::on_qcbOutputDevice_activated(int) {
	if (bInit)
		return;

	if (!AudioOutputRegistrar::qmNew)
		return;

	Audio::stopOutput();

	AudioOutputRegistrar *aor = AudioOutputRegistrar::qmNew->value(qcbOutput->currentText());
	int idx                   = qcbOutputDevice->currentIndex();
	if (idx > -1) {
		aor->setDeviceChoice(qcbOutputDevice->itemData(idx), g.s);
		bDelay = aor->usesOutputDelay();
	}

	AudioInputRegistrar *air              = AudioInputRegistrar::qmNew->value(qcbInput->currentText());
	EchoCancelOptionID echoCancelOptionId = firstUsableEchoCancellation(air, qcbOutput->currentText());
	qcbEcho->setEnabled(echoCancelOptionId != EchoCancelOptionID::DISABLED);

	g.ao = AudioOutputPtr(aor->create());
	g.ao->start(QThread::HighPriority);
}

void AudioWizard::on_qsOutputDelay_valueChanged(int v) {
	qlOutputDelay->setText(tr("%1 ms").arg(v * 10));
	g.s.iOutputDelay = v;
	restartAudio();
}

void AudioWizard::on_qsMaxAmp_valueChanged(int v) {
	g.s.iMinLoudness = qMin(v, 30000);
}

void AudioWizard::showPage(int pageid) {
	if (pageid == -1)
		return;

	CompletablePage *cp = qobject_cast< CompletablePage * >(currentPage());

	AudioOutputPtr ao = g.ao;
	if (ao)
		ao->wipe();
	aosSource = nullptr;

	g.bPosTest = false;

	if (cp == qwpIntro) {
		g.s.bMute = true;
	} else if (cp == qwpDone) {
		g.s.bMute = true;
	} else if (cp == qwpDeviceTuning) {
		g.s.bMute = true;
		playChord();
	} else if (cp == qwpPositional) {
		fX = fY    = 0.0f;
		g.s.bMute  = true;
		g.bPosTest = true;
		if (qgsScene) {
			delete qgsScene;
			qgiSource = nullptr;
			qgsScene  = nullptr;
		}
		playChord();
	} else {
		g.s.bMute = false;
	}

	if ((cp == qwpTrigger) || (cp == qwpSettings)) {
		if (!bTransmitChanged)
			g.s.atTransmit = sOldSettings.atTransmit;
		else if (qrPTT->isChecked())
			g.s.atTransmit = Settings::PushToTalk;
		else
			g.s.atTransmit = Settings::VAD;
	} else {
		g.s.atTransmit = Settings::Continuous;
	}
}

int AudioWizard::nextId() const {
	AudioOutputPtr ao = g.ao;

	int nextid = QWizard::nextId();
	if (currentPage() == qwpSettings && !g.s.bPositionalAudio)
		nextid++;
	else if ((currentPage() == qwpDevice) && !bDelay)
		nextid++;
	return nextid;
}

void AudioWizard::playChord() {
	AudioOutputPtr ao = g.ao;
	if (!ao || aosSource || bInit)
		return;
	aosSource = ao->playSample(QLatin1String(":/wb_male.oga"), true);
}

void AudioWizard::restartAudio() {
	aosSource = nullptr;

	Audio::stop();

	g.s.qsAudioInput  = qcbInput->currentText();
	g.s.qsAudioOutput = qcbOutput->currentText();

	Audio::start();

	if (qgsScene) {
		delete qgsScene;
		qgiSource = nullptr;
		qgsScene  = nullptr;
	}

	if ((currentPage() == qwpPositional) || (currentPage() == qwpDeviceTuning))
		playChord();
}

void AudioWizard::reject() {
	g.s = sOldSettings;

	g.s.lmLoopMode = Settings::None;
	restartAudio();

	AudioOutputPtr ao = g.ao;
	if (ao)
		ao->wipe();
	aosSource        = nullptr;
	g.bInAudioWizard = false;

	QWizard::reject();
}

void AudioWizard::accept() {
	if (!bTransmitChanged)
		g.s.atTransmit = sOldSettings.atTransmit;
	else if (qrPTT->isChecked())
		g.s.atTransmit = Settings::PushToTalk;
	else
		g.s.atTransmit = Settings::VAD;

	g.s.bMute      = sOldSettings.bMute;
	g.s.bDeaf      = sOldSettings.bDeaf;
	g.s.lmLoopMode = Settings::None;

	// Switch TTS<->Sounds according to user selection
	if (!qrbNotificationCustom->isChecked()) {
		Settings::MessageLog mlReplace = qrbNotificationTTS->isChecked() ? Settings::LogSoundfile : Settings::LogTTS;

		for (int i = Log::firstMsgType; i <= Log::lastMsgType; ++i) {
			if (g.s.qmMessages[i] & mlReplace)
				g.s.qmMessages[i] ^= Settings::LogSoundfile | Settings::LogTTS;
		}

		if (qrbNotificationTTS->isChecked()) {
			g.s.bTTS = true;
			g.mw->qaAudioTTS->setChecked(true);
		}
	}

	g.s.bUsage = qcbUsage->isChecked();
	g.bPosTest = false;
	restartAudio();
	g.bInAudioWizard = false;
	QWizard::accept();
}

bool AudioWizard::validateCurrentPage() {
	if (currentId() == 1) {
		if ((qcbInput->currentIndex() < 0) || (qcbOutput->currentIndex() < 0))
			return false;
	}
	return true;
}

void AudioWizard::on_Ticker_timeout() {
	AudioInputPtr ai  = g.ai;
	AudioOutputPtr ao = g.ao;
	if (!ai || !ao)
		return;

	int iPeak = static_cast< int >(ai->dMaxMic);

	if (iTicks++ >= 50) {
		iMaxPeak = 0;
		iTicks   = 0;
	}
	if (iPeak > iMaxPeak)
		iMaxPeak = iPeak;

	abAmplify->iBelow = qsMaxAmp->value();
	abAmplify->iValue = iPeak;
	abAmplify->iPeak  = iMaxPeak;
	abAmplify->update();

	abVAD->iBelow = iroundf(g.s.fVADmin * 32767.0f + 0.5f);
	abVAD->iAbove = iroundf(g.s.fVADmax * 32767.0f + 0.5f);

	if (g.s.vsVAD == Settings::Amplitude) {
		abVAD->iValue = iroundf((32767.f / 96.0f) * (96.0f + ai->dPeakCleanMic) + 0.5f);
	} else {
		abVAD->iValue = iroundf(ai->fSpeechProb * 32767.0f + 0.5f);
	}
	abVAD->update();

	bool active = ai->isTransmitting();
	if (active != bLastActive) {
		bLastActive = active;
		qlTalkIcon->setPixmap(active ? qpTalkingOn : qpTalkingOff);
	}

	if (!qgsScene) {
		const float baseRadius = 0.5;

		unsigned int nspeaker = 0;

		// Note: when updating these, make sure the colors in AudioWizard.ui match up.
		const QColor skyBlueColor(QLatin1String("#56b4e9"));
		const QColor bluishGreenColor(QLatin1String("#009e73"));
		const QColor vermillionColor(QLatin1String("#d55e00"));

		// Get the directions of the speakers as unit vectors and also the amount of them
		const float *spos = ao->getSpeakerPos(nspeaker);

		if ((nspeaker > 0) && spos) {
			qgsScene = new QGraphicsScene(QRectF(-4.0f, -4.0f, 8.0f, 8.0f), this);

			QPen pen;
			// A width of 0 will cause it to always use a width
			// of exactly 1 pixel
			pen.setWidth(0);

			QGraphicsEllipseItem *ownPos = qgsScene->addEllipse(
				QRectF(-baseRadius, -baseRadius, 2 * baseRadius, 2 * baseRadius), pen, QBrush(skyBlueColor));
			ownPos->setPos(0, 0);

			// Good for debugging: This draws a cross at the origin
			// qgsScene->addLine(QLineF(0,-1,0,1), pen);
			// qgsScene->addLine(QLineF(-1,0,1,0), pen);

			const float speakerScale  = 0.9;
			const float speakerRadius = baseRadius * speakerScale;

			// nspeaker is in format [x1,y1,z1, x2,y2,z2, ...]
			for (unsigned int i = 0; i < nspeaker; ++i) {
				if ((spos[3 * i] != 0.0f) || (spos[3 * i + 1] != 0.0f) || (spos[3 * i + 2] != 0.0f)) {
					float x = spos[3 * i];
					float z = spos[3 * i + 2];

					const float lengthInPlane = std::sqrt(x * x + z * z);

					// Scale the vector in the xz plane so that its length is at least enough for
					// the speaker icons and the icon for the own pos don't overlap
					if ((baseRadius + speakerRadius) < lengthInPlane) {
						const float scaleFactor = (baseRadius + speakerRadius) / lengthInPlane;

						x *= scaleFactor;
						z *= scaleFactor;
					}

					QGraphicsEllipseItem *ellipse = qgsScene->addEllipse(
						QRectF(-speakerRadius, -speakerRadius, 2 * speakerRadius, 2 * speakerRadius), pen,
						QBrush(vermillionColor));
					ellipse->setPos(x, z);
				}
			}

			const float sourceScale  = 0.9;
			const float sourceRadius = baseRadius * sourceScale;

			qgiSource = qgsScene->addEllipse(QRectF(-sourceRadius, -sourceRadius, 2 * sourceRadius, 2 * sourceRadius),
											 pen, QBrush(bluishGreenColor));
			qgiSource->setPos(0, (sourceRadius + baseRadius) * 1.5);

			qgvView->setScene(qgsScene);
			qgvView->fitInView(-4.0f, -4.0f, 8.0f, 8.0f, Qt::KeepAspectRatio);
		}
	} else if (currentPage() == qwpPositional) {
		// This block here is responsible for setting the position of
		// the audio source. Unless the user has clicked on the scene,
		// the source will rotate around the origin at a radius of 2.
		// Once the user has clicked on the scene, the sound source
		// is put where (s)he clicked last.
		float xp, yp;
		if ((fX == 0.0f) && (fY == 0.0f)) {
			// increase the angle of the sound source by a certain amount. he higher
			// this value is, the faster will the source rotate.
			fAngle += 0.02f;

			xp = sinf(fAngle) * 2.0f;
			yp = cosf(fAngle) * 2.0f;
		} else {
			xp = fX;
			yp = fY;
		}

		qgiSource->setPos(xp, yp);
		if (aosSource) {
			aosSource->fPos[0] = xp;
			aosSource->fPos[1] = 0;
			aosSource->fPos[2] = yp;
		}
	}
}

void AudioWizard::on_qsVAD_valueChanged(int v) {
	if (!bInit) {
		g.s.fVADmax = static_cast< float >(v) / 32767.0f;
		g.s.fVADmin = g.s.fVADmax * 0.9f;
	}
}

void AudioWizard::on_qrSNR_clicked(bool on) {
	if (on) {
		g.s.vsVAD      = Settings::SignalToNoise;
		g.s.atTransmit = Settings::VAD;
		updateTriggerWidgets(false);
		bTransmitChanged = true;
	}
}

void AudioWizard::on_qrAmplitude_clicked(bool on) {
	if (on) {
		g.s.vsVAD      = Settings::Amplitude;
		g.s.atTransmit = Settings::VAD;
		updateTriggerWidgets(false);
		bTransmitChanged = true;
	}
}

void AudioWizard::on_qrPTT_clicked(bool on) {
	if (on) {
		g.s.atTransmit = Settings::PushToTalk;
		updateTriggerWidgets(true);
		bTransmitChanged = true;
	}
}

void AudioWizard::on_qpbPTT_clicked() {
	GlobalShortcutButtons dialog;
	dialog.setButtons(pttButtons);

	const auto ret = dialog.exec();
	if (ret != Accepted) {
		return;
	}

	pttButtons = dialog.buttons();
	if (!pttButtons.isEmpty()) {
		qrPTT->setChecked(true);
		updateTriggerWidgets(true);
	} else if (qrPTT->isChecked()) {
		qrAmplitude->setChecked(true);
		updateTriggerWidgets(false);
	}

	bTransmitChanged = true;

	QList< Shortcut > shortcuts;
	bool found = false;
	for (auto &shortcut : g.s.qlShortcuts) {
		if (shortcut.iIndex == g.mw->gsPushTalk->idx) {
			if (pttButtons.isEmpty()) {
				continue;
			}

			if (!found) {
				found              = true;
				shortcut.qlButtons = pttButtons;
			}
		}

		shortcuts << shortcut;
	}

	if (!found && !pttButtons.isEmpty()) {
		Shortcut shortcut;
		shortcut.iIndex    = g.mw->gsPushTalk->idx;
		shortcut.qlButtons = pttButtons;
		shortcut.bSuppress = false;
		shortcuts << shortcut;
	}

	g.s.qlShortcuts                          = shortcuts;
	GlobalShortcutEngine::engine->bNeedRemap = true;
	GlobalShortcutEngine::engine->needRemap();
}

void AudioWizard::on_qcbEcho_clicked(bool on) {
	if (on) {
		AudioInputRegistrar *air = AudioInputRegistrar::qmNew->value(qcbInput->currentText());
		g.s.echoOption           = firstUsableEchoCancellation(air, qcbOutput->currentText());
	} else {
		g.s.echoOption = EchoCancelOptionID::DISABLED;
	}
	restartAudio();
}

void AudioWizard::on_qcbHeadphone_clicked(bool on) {
	g.s.bPositionalHeadphone = on;
	restartAudio();
}

void AudioWizard::on_qcbPositional_clicked(bool on) {
	g.s.bPositionalAudio  = on;
	g.s.bTransmitPosition = on;
	restartAudio();
}

void AudioWizard::updateTriggerWidgets(bool ptt) {
	qwVAD->setEnabled(!ptt);

	if (pttButtons.count() > 0) {
		QString text;
		for (const auto &button : pttButtons) {
			if (!text.isEmpty()) {
				text += ' ';
			}

			const auto info = GlobalShortcutEngine::engine->buttonInfo(button);
			text += QString("'%1%2'").arg(info.devicePrefix, info.name);
		}

		qpbPTT->setText(text);
		qwpTrigger->setComplete(true);
	} else {
		qpbPTT->setText(tr("No buttons assigned"));
		qwpTrigger->setComplete(!ptt);
	}
}

void AudioWizard::on_qcbAttenuateOthers_clicked(bool checked) {
	g.s.bAttenuateOthers = checked;
}

void AudioWizard::on_qcbHighContrast_clicked(bool on) {
	g.s.bHighContrast = on;

	qliAmpTuningText->setVisible(!g.s.bHighContrast);
	qliAmpTuningTextHC->setVisible(g.s.bHighContrast);

	qliVolumeTuningText->setVisible(!g.s.bHighContrast);
	qliVolumeTuningTextHC->setVisible(g.s.bHighContrast);

	qliVadTuningText->setVisible(!g.s.bHighContrast);
	qliVadTuningTextHC->setVisible(g.s.bHighContrast);
}

void AudioWizard::on_qrbQualityLow_clicked() {
	g.s.iQuality         = 16000;
	g.s.iFramesPerPacket = 6;
	restartAudio();
}

void AudioWizard::on_qrbQualityBalanced_clicked() {
	g.s.iQuality         = 40000;
	g.s.iFramesPerPacket = 2;
	restartAudio();
}

void AudioWizard::on_qrbQualityUltra_clicked() {
	g.s.iQuality         = 72000;
	g.s.iFramesPerPacket = 1;
	restartAudio();
}

void AudioWizard::on_qrbQualityCustom_clicked() {
	g.s.iQuality         = sOldSettings.iQuality;
	g.s.iFramesPerPacket = sOldSettings.iFramesPerPacket;
	restartAudio();
}

EchoCancelOptionID AudioWizard::firstUsableEchoCancellation(AudioInputRegistrar *air, const QString outputSys) {
	for (EchoCancelOptionID ecoid : air->echoOptions) {
		if (air->canEcho(ecoid, outputSys)) {
			return ecoid;
		}
	}

	return EchoCancelOptionID::DISABLED;
}
