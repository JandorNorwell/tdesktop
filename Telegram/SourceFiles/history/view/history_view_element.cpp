/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history_media.h"
#include "history/history_media_grouped.h"
#include "history/history.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "auth_session.h"
#include "layout.h"

namespace HistoryView {
namespace {

// A new message from the same sender is attached to previous within 15 minutes.
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

} // namespace

TextSelection UnshiftItemSelection(
		TextSelection selection,
		uint16 byLength) {
	return (selection == FullSelection)
		? selection
		: ::unshiftSelection(selection, byLength);
}

TextSelection ShiftItemSelection(
		TextSelection selection,
		uint16 byLength) {
	return (selection == FullSelection)
		? selection
		: ::shiftSelection(selection, byLength);
}

TextSelection UnshiftItemSelection(
		TextSelection selection,
		const Text &byText) {
	return UnshiftItemSelection(selection, byText.length());
}

TextSelection ShiftItemSelection(
		TextSelection selection,
		const Text &byText) {
	return ShiftItemSelection(selection, byText.length());
}

Element::Element(not_null<HistoryItem*> data, Context context)
: _data(data)
, _media(_data->media() ? _data->media()->createView(this) : nullptr)
, _context(context) {
	Auth().data().registerItemView(this);
	initGroup();
}

not_null<HistoryItem*> Element::data() const {
	return _data;
}

HistoryMedia *Element::media() const {
	return _media.get();
}

Context Element::context() const {
	return _context;
}

int Element::y() const {
	return _y;
}

void Element::setY(int y) {
	_y = y;
}

int Element::marginTop() const {
	const auto item = data();
	auto result = 0;
	if (!isHiddenByGroup()) {
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
	}
	result += item->displayedDateHeight();
	if (const auto unreadbar = item->Get<HistoryMessageUnreadBar>()) {
		result += unreadbar->height();
	}
	return result;
}

int Element::marginBottom() const {
	const auto item = data();
	return isHiddenByGroup() ? 0 : st::msgMargin.bottom();
}

bool Element::isUnderCursor() const {
	return (App::hoveredItem() == this);
}

void Element::setPendingResize() {
	_flags |= Flag::NeedsResize;
	if (_context == Context::History) {
		data()->_history->setHasPendingResizedItems();
	}
}

bool Element::pendingResize() const {
	return _flags & Flag::NeedsResize;
}

bool Element::isAttachedToPrevious() const {
	return _flags & Flag::AttachedToPrevious;
}

bool Element::isAttachedToNext() const {
	return _flags & Flag::AttachedToNext;
}

int Element::skipBlockWidth() const {
	return st::msgDateSpace + infoWidth() - st::msgDateDelta.x();
}

int Element::skipBlockHeight() const {
	return st::msgDateFont->height - st::msgDateDelta.y();
}

QString Element::skipBlock() const {
	return textcmdSkipBlock(skipBlockWidth(), skipBlockHeight());
}

int Element::infoWidth() const {
	return 0;
}

bool Element::isHiddenByGroup() const {
	return _flags & Flag::HiddenByGroup;
}

void Element::makeGroupMember(not_null<Element*> leader) {
	Expects(leader != this);

	const auto group = Get<Group>();
	Assert(group != nullptr);
	if (group->leader == this) {
		if (auto single = _media ? _media->takeLastFromGroup() : nullptr) {
			_media = std::move(single);
		}
		_flags |= Flag::HiddenByGroup;
		Auth().data().requestViewResize(this);

		group->leader = leader;
		base::take(group->others);
	} else if (group->leader != leader) {
		group->leader = leader;
	}

	Ensures(isHiddenByGroup());
	Ensures(group->others.empty());
}

void Element::makeGroupLeader(std::vector<not_null<Element*>> &&others) {
	const auto group = Get<Group>();
	Assert(group != nullptr);

	const auto leaderChanged = (group->leader != this);
	if (leaderChanged) {
		group->leader = this;
		_flags &= ~Flag::HiddenByGroup;
		Auth().data().requestViewResize(this);
	}
	group->others = std::move(others);
	if (!_media || !_media->applyGroup(group->others)) {
		resetGroupMedia(group->others);
		data()->invalidateChatsListEntry();
	}

	Ensures(!isHiddenByGroup());
}

bool Element::groupIdValidityChanged() {
	if (Has<Group>()) {
		if (_media && _media->canBeGrouped()) {
			return false;
		}
		RemoveComponents(Group::Bit());
		Auth().data().requestViewResize(this);
		return true;
	}
	return false;
}

void Element::validateGroupId() {
	// Just ignore the result.
	groupIdValidityChanged();
}

Group *Element::getFullGroup() {
	if (const auto group = Get<Group>()) {
		if (group->leader == this) {
			return group;
		}
		return group->leader->Get<Group>();
	}
	return nullptr;
}

void Element::initGroup() {
	if (const auto groupId = _data->groupId()) {
		AddComponents(Group::Bit());
		const auto group = Get<Group>();
		group->groupId = groupId;
		group->leader = this;
	}
}

void Element::resetGroupMedia(
		const std::vector<not_null<Element*>> &others) {
	if (!others.empty()) {
		_media = std::make_unique<HistoryGroupedMedia>(this, others);
	} else if (_media) {
		_media = _media->takeLastFromGroup();
	}
	Auth().data().requestViewResize(this);
}

void Element::previousInBlocksChanged() {
	recountDisplayDateInBlocks();
	recountAttachToPreviousInBlocks();
}

// Called only if there is no more next item! Not always when it changes!
void Element::nextInBlocksChanged() {
	setAttachToNext(false);
}

void Element::refreshDataId() {
	if (const auto media = this->media()) {
		media->refreshParentId(this);
		if (const auto group = Get<Group>()) {
			if (group->leader != this) {
				group->leader->refreshDataId();
			}
		}
	}
}

bool Element::computeIsAttachToPrevious(not_null<Element*> previous) {
	const auto item = data();
	if (!item->Has<HistoryMessageDate>() && !item->Has<HistoryMessageUnreadBar>()) {
		const auto prev = previous->data();
		const auto possible = !item->isPost() && !prev->isPost()
			&& !item->serviceMsg() && !prev->serviceMsg()
			&& !item->isEmpty() && !prev->isEmpty()
			&& (qAbs(prev->date.secsTo(item->date)) < kAttachMessageToPreviousSecondsDelta);
		if (possible) {
			if (item->history()->peer->isSelf()) {
				return prev->senderOriginal() == item->senderOriginal()
					&& (prev->Has<HistoryMessageForwarded>() == item->Has<HistoryMessageForwarded>());
			} else {
				return prev->from() == item->from();
			}
		}
	}
	return false;
}

void Element::recountAttachToPreviousInBlocks() {
	auto attachToPrevious = false;
	if (const auto previous = previousInBlocks()) {
		attachToPrevious = computeIsAttachToPrevious(previous);
		previous->setAttachToNext(attachToPrevious);
	}
	setAttachToPrevious(attachToPrevious);
}

void Element::recountDisplayDateInBlocks() {
	setDisplayDate([&] {
		const auto item = data();
		if (item->isEmpty()) {
			return false;
		}

		if (auto previous = previousInBlocks()) {
			const auto prev = previous->data();
			return prev->isEmpty() || (prev->date.date() != item->date.date());
		}
		return true;
	}());
}

QSize Element::countOptimalSize() {
	return performCountOptimalSize();
}

QSize Element::countCurrentSize(int newWidth) {
	if (_flags & Flag::NeedsResize) {
		_flags &= ~Flag::NeedsResize;
		initDimensions();
	}
	return performCountCurrentSize(newWidth);
}

void Element::setDisplayDate(bool displayDate) {
	const auto item = data();
	if (displayDate && !item->Has<HistoryMessageDate>()) {
		item->AddComponents(HistoryMessageDate::Bit());
		item->Get<HistoryMessageDate>()->init(item->date);
		setPendingResize();
	} else if (!displayDate && item->Has<HistoryMessageDate>()) {
		item->RemoveComponents(HistoryMessageDate::Bit());
		setPendingResize();
	}
}

void Element::setAttachToNext(bool attachToNext) {
	if (attachToNext && !(_flags & Flag::AttachedToNext)) {
		_flags |= Flag::AttachedToNext;
		Auth().data().requestItemRepaint(data());
	} else if (!attachToNext && (_flags & Flag::AttachedToNext)) {
		_flags &= ~Flag::AttachedToNext;
		Auth().data().requestItemRepaint(data());
	}
}

void Element::setAttachToPrevious(bool attachToPrevious) {
	if (attachToPrevious && !(_flags & Flag::AttachedToPrevious)) {
		_flags |= Flag::AttachedToPrevious;
		setPendingResize();
	} else if (!attachToPrevious && (_flags & Flag::AttachedToPrevious)) {
		_flags &= ~Flag::AttachedToPrevious;
		setPendingResize();
	}
}

bool Element::displayFromPhoto() const {
	return false;
}

bool Element::hasFromPhoto() const {
	return false;
}

bool Element::hasFromName() const {
	return false;
}

bool Element::displayFromName() const {
	return false;
}

bool Element::displayForwardedFrom() const {
	return false;
}

bool Element::hasOutLayout() const {
	return false;
}

bool Element::drawBubble() const {
	return false;
}

bool Element::hasBubble() const {
	return false;
}

bool Element::hasFastReply() const {
	return false;
}

bool Element::displayFastReply() const {
	return false;
}

bool Element::displayRightAction() const {
	return false;
}

void Element::drawRightAction(
	Painter &p,
	int left,
	int top,
	int outerWidth) const {
}

ClickHandlerPtr Element::rightActionLink() const {
	return ClickHandlerPtr();
}

bool Element::displayEditedBadge() const {
	return false;
}

QDateTime Element::displayedEditDate() const {
	return QDateTime();
}

HistoryBlock *Element::block() {
	return _block;
}

const HistoryBlock *Element::block() const {
	return _block;
}

void Element::attachToBlock(not_null<HistoryBlock*> block, int index) {
	Expects(!_data->isLogEntry());
	Expects(_block == nullptr);
	Expects(_indexInBlock < 0);
	Expects(index >= 0);

	_block = block;
	_indexInBlock = index;
	_data->setMainView(this);
}

void Element::removeFromBlock() {
	Expects(_block != nullptr);

	_block->remove(this);
}

void Element::setIndexInBlock(int index) {
	Expects(_block != nullptr);
	Expects(index >= 0);

	_indexInBlock = index;
}

int Element::indexInBlock() const {
	Expects((_indexInBlock >= 0) == (_block != nullptr));
	Expects((_block == nullptr) || (_block->messages[_indexInBlock].get() == this));

	return _indexInBlock;
}

Element *Element::previousInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock > 0) {
			return _block->messages[_indexInBlock - 1].get();
		}
		if (auto previous = _block->previousBlock()) {
			Assert(!previous->messages.empty());
			return previous->messages.back().get();
		}
	}
	return nullptr;
}

Element *Element::nextInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock + 1 < _block->messages.size()) {
			return _block->messages[_indexInBlock + 1].get();
		}
		if (auto next = _block->nextBlock()) {
			Assert(!next->messages.empty());
			return next->messages.front().get();
		}
	}
	return nullptr;
}

void Element::drawInfo(
	Painter &p,
	int right,
	int bottom,
	int width,
	bool selected,
	InfoDisplayType type) const {
}

bool Element::pointInTime(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const {
	return false;
}

TextSelection Element::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}

void Element::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerActiveChanged(handler, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	Auth().data().requestItemRepaint(_data);
	if (const auto media = this->media()) {
		media->clickHandlerActiveChanged(handler, active);
	}
}

void Element::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerPressedChanged(handler, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	Auth().data().requestItemRepaint(_data);
	if (const auto media = this->media()) {
		media->clickHandlerPressedChanged(handler, pressed);
	}
}

Element::~Element() {
	Auth().data().unregisterItemView(this);
}

} // namespace HistoryView