#include "TimelineModel.h"

#include <type_traits>

#include <QRegularExpression>

#include "Logging.h"
#include "Olm.h"
#include "Utils.h"
#include "dialogs/RawMessage.h"

namespace {
template<class T>
QString
eventId(const T &event)
{
        return QString::fromStdString(event.event_id);
}
template<class T>
QString
roomId(const T &event)
{
        return QString::fromStdString(event.room_id);
}
template<class T>
QString
senderId(const T &event)
{
        return QString::fromStdString(event.sender);
}

template<class T>
QDateTime
eventTimestamp(const T &event)
{
        return QDateTime::fromMSecsSinceEpoch(event.origin_server_ts);
}

template<class T>
QString
eventFormattedBody(const mtx::events::Event<T> &)
{
        return QString("");
}
template<class T>
auto
eventFormattedBody(const mtx::events::RoomEvent<T> &e)
  -> std::enable_if_t<std::is_same<decltype(e.content.formatted_body), std::string>::value, QString>
{
        auto temp = e.content.formatted_body;
        if (!temp.empty()) {
                auto pos = temp.find("<mx-reply>");
                if (pos != std::string::npos)
                        temp.erase(pos, std::string("<mx-reply>").size());
                pos = temp.find("</mx-reply>");
                if (pos != std::string::npos)
                        temp.erase(pos, std::string("</mx-reply>").size());
                return QString::fromStdString(temp);
        } else
                return QString::fromStdString(e.content.body);
}

template<class T>
QString
eventUrl(const T &)
{
        return "";
}
template<class T>
auto
eventUrl(const mtx::events::RoomEvent<T> &e)
  -> std::enable_if_t<std::is_same<decltype(e.content.url), std::string>::value, QString>
{
        return QString::fromStdString(e.content.url);
}

template<class T>
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<T> &e)
{
        using mtx::events::EventType;
        switch (e.type) {
        case EventType::RoomKeyRequest:
                return qml_mtx_events::EventType::KeyRequest;
        case EventType::RoomAliases:
                return qml_mtx_events::EventType::Aliases;
        case EventType::RoomAvatar:
                return qml_mtx_events::EventType::Avatar;
        case EventType::RoomCanonicalAlias:
                return qml_mtx_events::EventType::CanonicalAlias;
        case EventType::RoomCreate:
                return qml_mtx_events::EventType::Create;
        case EventType::RoomEncrypted:
                return qml_mtx_events::EventType::Encrypted;
        case EventType::RoomEncryption:
                return qml_mtx_events::EventType::Encryption;
        case EventType::RoomGuestAccess:
                return qml_mtx_events::EventType::GuestAccess;
        case EventType::RoomHistoryVisibility:
                return qml_mtx_events::EventType::HistoryVisibility;
        case EventType::RoomJoinRules:
                return qml_mtx_events::EventType::JoinRules;
        case EventType::RoomMember:
                return qml_mtx_events::EventType::Member;
        case EventType::RoomMessage:
                return qml_mtx_events::EventType::UnknownMessage;
        case EventType::RoomName:
                return qml_mtx_events::EventType::Name;
        case EventType::RoomPowerLevels:
                return qml_mtx_events::EventType::PowerLevels;
        case EventType::RoomTopic:
                return qml_mtx_events::EventType::Topic;
        case EventType::RoomTombstone:
                return qml_mtx_events::EventType::Tombstone;
        case EventType::RoomRedaction:
                return qml_mtx_events::EventType::Redaction;
        case EventType::RoomPinnedEvents:
                return qml_mtx_events::EventType::PinnedEvents;
        case EventType::Sticker:
                return qml_mtx_events::EventType::Sticker;
        case EventType::Tag:
                return qml_mtx_events::EventType::Tag;
        case EventType::Unsupported:
        default:
                return qml_mtx_events::EventType::Unsupported;
        }
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Audio> &)
{
        return qml_mtx_events::EventType::AudioMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Emote> &)
{
        return qml_mtx_events::EventType::EmoteMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::File> &)
{
        return qml_mtx_events::EventType::FileMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Image> &)
{
        return qml_mtx_events::EventType::ImageMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Notice> &)
{
        return qml_mtx_events::EventType::NoticeMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Text> &)
{
        return qml_mtx_events::EventType::TextMessage;
}
qml_mtx_events::EventType
toRoomEventType(const mtx::events::Event<mtx::events::msg::Video> &)
{
        return qml_mtx_events::EventType::VideoMessage;
}
// ::EventType::Type toRoomEventType(const Event<mtx::events::msg::Location> &e) { return
// ::EventType::LocationMessage; }

template<class T>
uint64_t
eventHeight(const mtx::events::Event<T> &)
{
        return -1;
}
template<class T>
auto
eventHeight(const mtx::events::RoomEvent<T> &e) -> decltype(e.content.info.h)
{
        return e.content.info.h;
}
template<class T>
uint64_t
eventWidth(const mtx::events::Event<T> &)
{
        return -1;
}
template<class T>
auto
eventWidth(const mtx::events::RoomEvent<T> &e) -> decltype(e.content.info.w)
{
        return e.content.info.w;
}

template<class T>
double
eventPropHeight(const mtx::events::RoomEvent<T> &e)
{
        auto w = eventWidth(e);
        if (w == 0)
                w = 1;
        return eventHeight(e) / (double)w;
}
}

TimelineModel::TimelineModel(QString room_id, QObject *parent)
  : QAbstractListModel(parent)
  , room_id_(room_id)
{
        connect(
          this, &TimelineModel::oldMessagesRetrieved, this, &TimelineModel::addBackwardsEvents);
}

QHash<int, QByteArray>
TimelineModel::roleNames() const
{
        return {
          {Section, "section"},
          {Type, "type"},
          {Body, "body"},
          {FormattedBody, "formattedBody"},
          {UserId, "userId"},
          {UserName, "userName"},
          {Timestamp, "timestamp"},
          {Url, "url"},
          {Height, "height"},
          {Width, "width"},
          {ProportionalHeight, "proportionalHeight"},
          {Id, "id"},
        };
}
int
TimelineModel::rowCount(const QModelIndex &parent) const
{
        Q_UNUSED(parent);
        return (int)this->eventOrder.size();
}

QVariant
TimelineModel::data(const QModelIndex &index, int role) const
{
        if (index.row() < 0 && index.row() >= (int)eventOrder.size())
                return QVariant();

        QString id = eventOrder[index.row()];

        mtx::events::collections::TimelineEvents event = events.value(id);

        if (auto e = boost::get<mtx::events::EncryptedEvent<mtx::events::msg::Encrypted>>(&event)) {
                event = decryptEvent(*e).event;
        }

        switch (role) {
        case Section: {
                QDateTime date = boost::apply_visitor(
                  [](const auto &e) -> QDateTime { return eventTimestamp(e); }, event);
                date.setTime(QTime());

                QString userId =
                  boost::apply_visitor([](const auto &e) -> QString { return senderId(e); }, event);

                for (int r = index.row() - 1; r > 0; r--) {
                        QDateTime prevDate = boost::apply_visitor(
                          [](const auto &e) -> QDateTime { return eventTimestamp(e); },
                          events.value(eventOrder[r]));
                        prevDate.setTime(QTime());
                        if (prevDate != date)
                                return QString("%2 %1").arg(date.toMSecsSinceEpoch()).arg(userId);

                        QString prevUserId =
                          boost::apply_visitor([](const auto &e) -> QString { return senderId(e); },
                                               events.value(eventOrder[r]));
                        if (userId != prevUserId)
                                break;
                }

                return QString("%1").arg(userId);
        }
        case UserId:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> QString { return senderId(e); }, event));
        case UserName:
                return QVariant(displayName(boost::apply_visitor(
                  [](const auto &e) -> QString { return senderId(e); }, event)));

        case Timestamp:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> QDateTime { return eventTimestamp(e); }, event));
        case Type:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> qml_mtx_events::EventType { return toRoomEventType(e); },
                  event));
        case FormattedBody:
                return QVariant(utils::replaceEmoji(boost::apply_visitor(
                  [](const auto &e) -> QString { return eventFormattedBody(e); }, event)));
        case Url:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> QString { return eventUrl(e); }, event));
        case Height:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> qulonglong { return eventHeight(e); }, event));
        case Width:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> qulonglong { return eventWidth(e); }, event));
        case ProportionalHeight:
                return QVariant(boost::apply_visitor(
                  [](const auto &e) -> double { return eventPropHeight(e); }, event));
        case Id:
                return id;
        default:
                return QVariant();
        }
}

void
TimelineModel::addEvents(const mtx::responses::Timeline &timeline)
{
        if (isInitialSync) {
                prev_batch_token_ = QString::fromStdString(timeline.prev_batch);
                isInitialSync     = false;
        }

        if (timeline.events.empty())
                return;

        std::vector<QString> ids;
        for (const auto &e : timeline.events) {
                QString id =
                  boost::apply_visitor([](const auto &e) -> QString { return eventId(e); }, e);

                this->events.insert(id, e);
                ids.push_back(id);
        }

        beginInsertRows(QModelIndex(),
                        static_cast<int>(this->eventOrder.size()),
                        static_cast<int>(this->eventOrder.size() + ids.size() - 1));
        this->eventOrder.insert(this->eventOrder.end(), ids.begin(), ids.end());
        endInsertRows();
}

void
TimelineModel::fetchHistory()
{
        if (paginationInProgress) {
                nhlog::ui()->warn("Already loading older messages");
                return;
        }

        paginationInProgress = true;
        mtx::http::MessagesOpts opts;
        opts.room_id = room_id_.toStdString();
        opts.from    = prev_batch_token_.toStdString();

        nhlog::ui()->info("Paginationg room {}", opts.room_id);

        http::client()->messages(
          opts, [this, opts](const mtx::responses::Messages &res, mtx::http::RequestErr err) {
                  if (err) {
                          nhlog::net()->error("failed to call /messages ({}): {} - {}",
                                              opts.room_id,
                                              mtx::errors::to_string(err->matrix_error.errcode),
                                              err->matrix_error.error);
                          return;
                  }

                  emit oldMessagesRetrieved(std::move(res));
          });
}

void
TimelineModel::addBackwardsEvents(const mtx::responses::Messages &msgs)
{
        std::vector<QString> ids;
        for (const auto &e : msgs.chunk) {
                QString id =
                  boost::apply_visitor([](const auto &e) -> QString { return eventId(e); }, e);

                this->events.insert(id, e);
                ids.push_back(id);
        }

        beginInsertRows(QModelIndex(), 0, static_cast<int>(ids.size() - 1));
        this->eventOrder.insert(this->eventOrder.begin(), ids.rbegin(), ids.rend());
        endInsertRows();

        prev_batch_token_ = QString::fromStdString(msgs.end);

        paginationInProgress = false;
}

QColor
TimelineModel::userColor(QString id, QColor background)
{
        if (!userColors.contains(id))
                userColors.insert(
                  id, QColor(utils::generateContrastingHexColor(id, background.name())));
        return userColors.value(id);
}

QString
TimelineModel::displayName(QString id) const
{
        return Cache::displayName(room_id_, id);
}

QString
TimelineModel::avatarUrl(QString id) const
{
        return Cache::avatarUrl(room_id_, id);
}

QString
TimelineModel::formatDateSeparator(QDate date) const
{
        auto now = QDateTime::currentDateTime();

        QString fmt = QLocale::system().dateFormat(QLocale::LongFormat);

        if (now.date().year() == date.year()) {
                QRegularExpression rx("[^a-zA-Z]*y+[^a-zA-Z]*");
                fmt = fmt.remove(rx);
        }

        return date.toString(fmt);
}

QString
TimelineModel::escapeEmoji(QString str) const
{
        return utils::replaceEmoji(str);
}

void
TimelineModel::viewRawMessage(QString id) const
{
        std::string ev = utils::serialize_event(events.value(id)).dump(4);
        auto dialog    = new dialogs::RawMessage(QString::fromStdString(ev));
        Q_UNUSED(dialog);
}

DecryptionResult
TimelineModel::decryptEvent(const mtx::events::EncryptedEvent<mtx::events::msg::Encrypted> &e) const
{
        MegolmSessionIndex index;
        index.room_id    = room_id_.toStdString();
        index.session_id = e.content.session_id;
        index.sender_key = e.content.sender_key;

        mtx::events::RoomEvent<mtx::events::msg::Notice> dummy;
        dummy.origin_server_ts = e.origin_server_ts;
        dummy.event_id         = e.event_id;
        dummy.sender           = e.sender;
        dummy.content.body =
          tr("-- Encrypted Event (No keys found for decryption) --",
             "Placeholder, when the message was not decrypted yet or can't be decrypted")
            .toStdString();

        try {
                if (!cache::client()->inboundMegolmSessionExists(index)) {
                        nhlog::crypto()->info("Could not find inbound megolm session ({}, {}, {})",
                                              index.room_id,
                                              index.session_id,
                                              e.sender);
                        // TODO: request megolm session_id & session_key from the sender.
                        return {dummy, false};
                }
        } catch (const lmdb::error &e) {
                nhlog::db()->critical("failed to check megolm session's existence: {}", e.what());
                dummy.content.body = tr("-- Decryption Error (failed to communicate with DB) --",
                                        "Placeholder, when the message can't be decrypted, because "
                                        "the DB access failed when trying to lookup the session.")
                                       .toStdString();
                return {dummy, false};
        }

        std::string msg_str;
        try {
                auto session = cache::client()->getInboundMegolmSession(index);
                auto res     = olm::client()->decrypt_group_message(session, e.content.ciphertext);
                msg_str      = std::string((char *)res.data.data(), res.data.size());
        } catch (const lmdb::error &e) {
                nhlog::db()->critical("failed to retrieve megolm session with index ({}, {}, {})",
                                      index.room_id,
                                      index.session_id,
                                      index.sender_key,
                                      e.what());
                dummy.content.body =
                  tr("-- Decryption Error (failed to retrieve megolm keys from db) --",
                     "Placeholder, when the message can't be decrypted, because the DB access "
                     "failed.")
                    .toStdString();
                return {dummy, false};
        } catch (const mtx::crypto::olm_exception &e) {
                nhlog::crypto()->critical("failed to decrypt message with index ({}, {}, {}): {}",
                                          index.room_id,
                                          index.session_id,
                                          index.sender_key,
                                          e.what());
                dummy.content.body =
                  tr("-- Decryption Error (%1) --",
                     "Placeholder, when the message can't be decrypted. In this case, the Olm "
                     "decrytion returned an error, which is passed ad %1")
                    .arg(e.what())
                    .toStdString();
                return {dummy, false};
        }

        // Add missing fields for the event.
        json body                = json::parse(msg_str);
        body["event_id"]         = e.event_id;
        body["sender"]           = e.sender;
        body["origin_server_ts"] = e.origin_server_ts;
        body["unsigned"]         = e.unsigned_data;

        nhlog::crypto()->debug("decrypted event: {}", e.event_id);

        json event_array = json::array();
        event_array.push_back(body);

        std::vector<mtx::events::collections::TimelineEvents> events;
        mtx::responses::utils::parse_timeline_events(event_array, events);

        if (events.size() == 1)
                return {events.at(0), true};

        dummy.content.body =
          tr("-- Encrypted Event (Unknown event type) --",
             "Placeholder, when the message was decrypted, but we couldn't parse it, because "
             "Nheko/mtxclient don't support that event type yet")
            .toStdString();
        return {dummy, false};
}
