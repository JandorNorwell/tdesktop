/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
class RoundButton;
class LinkButton;
class SlideAnimation;
class CrossFadeAnimation;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Window {
class ConnectingWidget;
} // namespace Window

namespace Intro {

class Widget : public Ui::RpWidget, private MTP::Sender, private base::Subscriber {
	Q_OBJECT

public:
	Widget(QWidget *parent);

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

	void setInnerFocus();

	~Widget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

signals:
	void countryChanged();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
private slots:
	void onCheckUpdateStatus();
#endif // TDESKTOP_DISABLE_AUTOUPDATE

	// Internal interface.
public:
	struct Data {
		QString country;
		QString phone;
		QByteArray phoneHash;
		bool phoneIsRegistered = false;

		enum class CallStatus {
			Waiting,
			Calling,
			Called,
			Disabled,
		};
		CallStatus callStatus = CallStatus::Disabled;
		int callTimeout = 0;

		QString code;
		int codeLength = 5;
		bool codeByTelegram = false;

		QByteArray pwdSalt;
		bool hasRecovery = false;
		QString pwdHint;

		TextWithEntities termsText;
		bool termsPopup = false;
		int termsAge = 0;

		base::Observable<void> updated;

	};

	enum class Direction {
		Back,
		Forward,
		Replace,
	};
	class Step : public TWidget, public RPCSender, protected base::Subscriber {
	public:
		Step(QWidget *parent, Data *data, bool hasCover = false);

		virtual void finishInit() {
		}
		virtual void setInnerFocus() {
			setFocus();
		}

		void setGoCallback(
			base::lambda<void(Step *step, Direction direction)> callback);
		void setShowResetCallback(base::lambda<void()> callback);
		void setShowTermsCallback(
			base::lambda<void()> callback);
		void setAcceptTermsCallback(
			base::lambda<void(base::lambda<void()> callback)> callback);

		void prepareShowAnimated(Step *after);
		void showAnimated(Direction direction);
		void showFast();
		bool animating() const;

		bool hasCover() const;
		virtual bool hasBack() const;
		virtual void activate();
		virtual void cancelled();
		virtual void finished();

		virtual void submit() = 0;
		virtual QString nextButtonText() const;

		int contentLeft() const;
		int contentTop() const;

		void setErrorCentered(bool centered);
		void setErrorBelowLink(bool below);
		void showError(base::lambda<QString()> textFactory);
		void hideError() {
			showError(base::lambda<QString()>());
		}

		~Step();

	protected:
		void paintEvent(QPaintEvent *e) override;
		void resizeEvent(QResizeEvent *e) override;

		void setTitleText(base::lambda<QString()> richTitleTextFactory);
		void setDescriptionText(base::lambda<QString()> richDescriptionTextFactory);
		bool paintAnimated(Painter &p, QRect clip);

		void fillSentCodeData(const MTPDauth_sentCode &type);

		void showDescription();
		void hideDescription();

		Data *getData() const {
			return _data;
		}
		void finish(const MTPUser &user, QImage &&photo = QImage());

		void goBack() {
			if (_goCallback) _goCallback(nullptr, Direction::Back);
		}
		void goNext(Step *step) {
			if (_goCallback) _goCallback(step, Direction::Forward);
		}
		void goReplace(Step *step) {
			if (_goCallback) _goCallback(step, Direction::Replace);
		}
		void showResetButton() {
			if (_showResetCallback) _showResetCallback();
		}
		void showTerms() {
			if (_showTermsCallback) _showTermsCallback();
		}
		void acceptTerms(base::lambda<void()> callback) {
			if (_acceptTermsCallback) {
				_acceptTermsCallback(callback);
			}
		}

	private:
		struct CoverAnimation {
			CoverAnimation() = default;
			CoverAnimation(CoverAnimation &&other) = default;
			CoverAnimation &operator=(CoverAnimation &&other) = default;
			~CoverAnimation();

			std::unique_ptr<Ui::CrossFadeAnimation> title;
			std::unique_ptr<Ui::CrossFadeAnimation> description;

			// From content top till the next button top.
			QPixmap contentSnapshotWas;
			QPixmap contentSnapshotNow;
		};
		void updateLabelsPosition();
		void paintContentSnapshot(Painter &p, const QPixmap &snapshot, float64 alpha, float64 howMuchHidden);
		void refreshError();
		void refreshTitle();
		void refreshDescription();
		void refreshLang();

		CoverAnimation prepareCoverAnimation(Step *step);
		QPixmap prepareContentSnapshot();
		QPixmap prepareSlideAnimation();
		void showFinished();

		void prepareCoverMask();
		void paintCover(Painter &p, int top);

		Data *_data = nullptr;
		bool _hasCover = false;
		base::lambda<void(Step *step, Direction direction)> _goCallback;
		base::lambda<void()> _showResetCallback;
		base::lambda<void()> _showTermsCallback;
		base::lambda<void(
			base::lambda<void()> callback)> _acceptTermsCallback;

		object_ptr<Ui::FlatLabel> _title;
		base::lambda<QString()> _titleTextFactory;
		object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _description;
		base::lambda<QString()> _descriptionTextFactory;

		bool _errorCentered = false;
		bool _errorBelowLink = false;
		base::lambda<QString()> _errorTextFactory;
		object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };

		Animation _a_show;
		CoverAnimation _coverAnimation;
		std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
		QPixmap _coverMask;

	};

private:
	void setupConnectingWidget();
	void refreshLang();
	void animationCallback();
	void createLanguageLink();

	void updateControlsGeometry();
	Data *getData() {
		return &_data;
	}

	void fixOrder();
	void showControls();
	void hideControls();
	QRect calculateStepRect() const;

	void showResetButton();
	void resetAccount();

	void showTerms();
	void acceptTerms(base::lambda<void()> callback);
	void hideAndDestroy(object_ptr<Ui::FadeWrap<Ui::RpWidget>> widget);

	Step *getStep(int skip = 0) {
		Assert(_stepHistory.size() + skip > 0);
		return _stepHistory.at(_stepHistory.size() - skip - 1);
	}
	void historyMove(Direction direction);
	void moveToStep(Step *step, Direction direction);
	void appendStep(Step *step);

	void getNearestDC();
	void showTerms(base::lambda<void()> callback);

	Animation _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

	QVector<Step*> _stepHistory;

	Data _data;

	Animation _coverShownAnimation;
	int _nextTopFrom = 0;
	int _controlsTopFrom = 0;

	object_ptr<Ui::FadeWrap<Ui::IconButton>> _back;
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _update = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _settings;

	object_ptr<Ui::RoundButton> _next;
	object_ptr<Ui::FadeWrap<Ui::LinkButton>> _changeLanguage = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _resetAccount = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _terms = { nullptr };

	base::unique_qptr<Window::ConnectingWidget> _connecting;

	mtpRequestId _resetRequest = 0;

};

} // namespace Intro
