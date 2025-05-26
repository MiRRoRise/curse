#ifndef CHAT_CLIENT_HPP
#define CHAT_CLIENT_HPP

#include <QMainWindow>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QTabWidget>
#include <QInputDialog>
#include <boost/asio.hpp>
#include <portaudio.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <optional>
#include <QStyledItemDelegate>

using boost::asio::ip::udp;

class ChatItemDelegate;

class ChatClient : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatClient(QWidget *parent = nullptr);
    ~ChatClient();

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onRegisterButtonClicked();
    void onLoginButtonClicked();
    void onSendMessageButtonClicked();
    void onCreateChatButtonClicked();
    void onDeleteFromChatButtonClicked();
    void onDeleteAccountButtonClicked();
    void onStartCallButtonClicked();
    void onStopCallButtonClicked();
    void onSearchUsersButtonClicked();
    void onAddFriendButtonClicked();
    void onAcceptFriendRequestClicked();
    void onRejectFriendRequestClicked();
    void onInviteToChatButtonClicked();
    void onFriendDoubleClicked(QListWidgetItem *item);
    void onFriendClicked(QListWidgetItem *item);
    void onChatSelected(QListWidgetItem *item);
    void tryReconnect();
    void onUpdateAccountButtonClicked();
    void onDeleteVoiceChatButtonClicked();
    void onLogoutButtonClicked();
    void onDeleteFriendButtonClicked();
    void requestChatList();

private:
    void setupUi();
    void initializeWebSocket();
    void initializeVoIP();
    void showAuthScreen();
    void showChatScreen();
    void sendJsonMessage(const QJsonObject &message);
    void validateInput(const QString &login, const QString &password, const QString &name = "");
    void updateChatList(const QJsonArray &chats);
    void updateMessageList(const QJsonArray &messages);
    void appendMessage(const QString &userName, const QString &text, qint64 date);
    void handleErrorMessage(const QString &message);
    void sendSearchUsersRequest(const QString &searchTerm);
    void onSearchResultReceived(const QJsonArray &users);
    void requestFriendsList();
    void onFriendsListReceived(const QJsonArray &friends);
    void onFriendRequestsReceived(const QJsonArray &requests);
    bool checkConnection(const QString &channel_id);
    bool registerWithServer(const QString &channel_id);
    void initPortAudio();
    void terminatePortAudio();
    static int portAudioCallback(const void *inputBuffer, void *outputBuffer,
                                 unsigned long framesPerBuffer,
                                 const PaStreamCallbackTimeInfo *timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void *userData);
    void sendPing();
    void sendAudio();
    void receive();
    void startReceive();
    void handleReceive(std::size_t bytes_recvd);
    void stop();

    // Поля класса
    QWebSocket *webSocket;
    boost::asio::io_context io_context_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    udp::resolver resolver_; 
    bool isInitialSubscription; 
    QMap<int, QString> lastMessages;
    QMap<int, QSet<int>> displayedMessageIds;
    QMap<int, QList<QJsonObject>> chatMessages;
    QTimer *reconnectTimer;
    QString serverUrl = "ws://127.0.0.1:8080";
    bool isConnected = false;
    bool isAuthenticated = false;
    QString savedLogin;
    QString savedPassword;
    int currentChatId = -1;
    int currentUserId = -1;
    int lastProcessedChatId = -1;
    QLineEdit *newNameInput;
    QLineEdit *newPasswordInput;

    // UI элементы
    QWidget *authWidget;
    QLineEdit *loginEdit;
    QLineEdit *passwordEdit;
    QLineEdit *nameEdit;
    QPushButton *registerButton;
    QPushButton *loginButton;
    QPushButton *logoutButton;
    QPushButton *deleteFriendButton;
    QWidget *chatWidget;
    QTabWidget *mainTabs;
    QListWidget *chatListWidget;
    QListWidget *friendsList;
    QTextEdit *messageDisplay;
    QLineEdit *messageInput;
    QPushButton *sendMessageButton;
    QLineEdit *chatNameInput;
    QPushButton *createChatButton;
    QPushButton *inviteToChatButton;
    QPushButton *deleteFromChatButton;
    QPushButton *deleteAccountButton;
    QLabel *connectionStatusLabel;
    QLineEdit *searchUserInput;
    QPushButton *searchUsersButton;
    QListWidget *searchResultsList;
    QPushButton *addFriendButton;
    QListWidget *friendRequestsList;
    QPushButton *acceptFriendButton;
    QPushButton *rejectFriendButton;
    QPushButton *startCallButton;
    QPushButton *stopCallButton;
    QLabel *voipStatusLabel;
    QPushButton *updateAccountButton; 
    QPushButton *deleteVoiceChatButton;

    // VoIP
    udp::resolver::results_type endpoints_;
    std::unique_ptr<udp::socket> socket_;
    udp::endpoint endpoint_;
    udp::endpoint remote_endpoint_;
    std::array<char, 65536> buffer_;
    PaStream *stream_ = nullptr;
    QString channel_id_;
    std::atomic<bool> running_ = false;
    std::thread send_thread_;
    std::thread receive_thread_;
    std::thread keepalive_thread_;
    std::mutex audio_mutex_;
    std::mutex playback_mutex_;
    std::condition_variable audio_cv_;
    std::condition_variable playback_cv_;
    std::queue<std::vector<int16_t>> audioQueue_;
    std::queue<std::vector<int16_t>> playbackQueue_;
};

class ChatItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ChatItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

#endif // CHAT_CLIENT_HPP
