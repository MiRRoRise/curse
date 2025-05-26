#include "chat_client.hpp"
#include <QMessageBox>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>
#include <QApplication>
#include <QDateTime>
#include <QSettings>
#include <QScrollBar>
#include <QScrollArea>
#include <QPainter>
#include <QTimer>

ChatClient::ChatClient(QWidget *parent)
    : QMainWindow(parent),
      webSocket(new QWebSocket),
      strand_(io_context_.get_executor()),
      resolver_(io_context_),
      isInitialSubscription(true)
{
    setWindowTitle("Chat Client");
    setMinimumSize(800, 600);
    setupUi();
    initializeWebSocket();
    initializeVoIP();
    showAuthScreen();

    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(5000);
    chatListWidget->setItemDelegate(new ChatItemDelegate(this));
    connect(reconnectTimer, &QTimer::timeout, this, &ChatClient::tryReconnect);
}

ChatClient::~ChatClient()
{
    stop();
    webSocket->close();
    delete webSocket;
    delete reconnectTimer;
}

void ChatClient::initializeWebSocket()
{
    connect(webSocket, &QWebSocket::connected, this, &ChatClient::onConnected);
    connect(webSocket, &QWebSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(webSocket, &QWebSocket::textMessageReceived, this, &ChatClient::onTextMessageReceived);
    connect(webSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qDebug() << "WebSocket error:" << error << webSocket->errorString();
        connectionStatusLabel->setText("Chat: Error");
        connectionStatusLabel->setStyleSheet("color: red;");
        if (!isAuthenticated) {
            QMessageBox::critical(this, "Error", QString("Chat server connection failed: %1").arg(webSocket->errorString()));
        }
    });
}

void ChatClient::initializeVoIP()
{
    socket_ = std::make_unique<udp::socket>(io_context_);
    socket_->open(udp::v4());
    socket_->bind(udp::endpoint(udp::v4(), 0));
    endpoints_ = resolver_.resolve(udp::v4(), "127.0.0.1", "5004");
    endpoint_ = *endpoints_.begin();
    voipStatusLabel->setText("VoIP: Disconnected");
    voipStatusLabel->setStyleSheet("color: red;");
    qDebug() << "VoIP initialized, ready to connect to" << endpoint_.address().to_string().c_str() << ":" << endpoint_.port();
}

void ChatClient::tryReconnect()
{
    if (webSocket->state() != QAbstractSocket::ConnectedState && isAuthenticated) {
        if (savedLogin.isEmpty() || savedPassword.isEmpty()) {
            qDebug() << "Reconnection impossible: login or password empty";
            showAuthScreen();
            return;
        }
        qDebug() << "Reconnecting to chat server...";
        QUrlQuery query;
        query.addQueryItem("login", savedLogin);
        query.addQueryItem("password", savedPassword);
        QUrl url(serverUrl);
        url.setQuery(query);
        webSocket->open(url);
    }
}

void ChatClient::onConnected()
{
    qDebug() << "Connected to chat server";
    isConnected = true;
    reconnectTimer->stop();
    connectionStatusLabel->setText("Chat: Connected");
    connectionStatusLabel->setStyleSheet("color: green;");

    if (isAuthenticated) {
        QJsonObject message;
        message["topic"] = 2;
        message["ty"] = 2;
        sendJsonMessage(message);
        requestFriendsList();
    }
}

void ChatItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();

    // Фон
    if (opt.state & QStyle::State_Selected) {
        painter->fillRect(opt.rect, QColor("#40444B"));
    } else {
        painter->fillRect(opt.rect, QColor("#2C2F33"));
    }

    // Иконка для голосового чата
    bool isVoiceChat = index.data(Qt::UserRole + 1).toBool();
    if (isVoiceChat) {
        QIcon icon(":/icons/voice_icon.png");
        if (!icon.isNull()) {
            QPixmap pixmap = icon.pixmap(QSize(16, 16));
            painter->drawPixmap(opt.rect.left() + 5, opt.rect.top() + 5, pixmap);
        }
    }

    // Текст
    QFont font = opt.font;
    font.setPointSize(10);
    painter->setFont(font);

    QString text = index.data(Qt::DisplayRole).toString();
    QStringList lines = text.split("\n");
    int textX = opt.rect.left() + (isVoiceChat ? 25 : 5);
    int textY = opt.rect.top() + 15;

    if (lines.size() >= 2) {
        painter->setPen(QColor("#FFFFFF"));
        painter->drawText(textX, textY, opt.rect.width() - 10 - textX, 15, Qt::AlignLeft | Qt::AlignVCenter, lines[0]);
        painter->setPen(QColor("#99AAB5"));
        painter->drawText(textX, textY + 15, opt.rect.width() - 10 - textX, 15, Qt::AlignLeft | Qt::AlignVCenter, lines[1]);
    } else {
        painter->setPen(QColor("#FFFFFF"));
        painter->drawText(textX, textY, opt.rect.width() - 10 - textX, 30, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    painter->restore();
}

QSize ChatItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(200, 48);
}

void ChatClient::setupUi()
{
    resize(900, 600);

    // Экран авторизации
    authWidget = new QWidget(this);
    loginEdit = new QLineEdit(authWidget);
    loginEdit->setPlaceholderText("Логин");
    passwordEdit = new QLineEdit(authWidget);
    passwordEdit->setPlaceholderText("Пароль");
    passwordEdit->setEchoMode(QLineEdit::Password);
    nameEdit = new QLineEdit(authWidget);
    nameEdit->setPlaceholderText("Имя (для регистрации)");
    registerButton = new QPushButton("Регистрация", authWidget);
    loginButton = new QPushButton("Вход", authWidget);
    logoutButton = new QPushButton("Выйти", authWidget);

    QVBoxLayout *authLayout = new QVBoxLayout(authWidget);
    authLayout->addWidget(loginEdit);
    authLayout->addWidget(passwordEdit);
    authLayout->addWidget(nameEdit);
    authLayout->addWidget(registerButton);
    authLayout->addWidget(loginButton);
    authLayout->addWidget(logoutButton);
    authWidget->setLayout(authLayout);

    // Экран чата
    chatWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(chatWidget);

    // Левая панель
    QWidget *leftPanel = new QWidget(chatWidget);
    leftPanel->setFixedWidth(70);
    QVBoxLayout *leftPanelLayout = new QVBoxLayout(leftPanel);
    leftPanelLayout->setSpacing(5);
    leftPanelLayout->setContentsMargins(0, 0, 0, 0);

    chatListWidget = new QListWidget(leftPanel);
    chatListWidget->setIconSize(QSize(40, 40));
    chatListWidget->setMinimumWidth(60);
    chatListWidget->setMaximumWidth(70);
    chatListWidget->setStyleSheet("QListWidget { background: transparent; border: none; }");
    leftPanelLayout->addWidget(chatListWidget);
    leftPanel->setLayout(leftPanelLayout);

    // Основная область
    QWidget *mainArea = new QWidget(chatWidget);
    QVBoxLayout *mainAreaLayout = new QVBoxLayout(mainArea);

    mainTabs = new QTabWidget(mainArea);
    mainTabs->setTabPosition(QTabWidget::North);

    // Вкладка 1: Чаты
    QWidget *chatsTab = new QWidget();
    QVBoxLayout *chatsLayout = new QVBoxLayout(chatsTab);
    chatsLayout->setSpacing(10);

    messageDisplay = new QTextEdit(chatsTab);
    messageDisplay->setReadOnly(true);
    QScrollArea *messageScroll = new QScrollArea(chatsTab);
    messageScroll->setWidget(messageDisplay);
    messageScroll->setWidgetResizable(true);

    messageInput = new QLineEdit(chatsTab);
    messageInput->setPlaceholderText("Введите сообщение...");
    sendMessageButton = new QPushButton("➤", chatsTab);

    QHBoxLayout *messageInputLayout = new QHBoxLayout();
    messageInputLayout->addWidget(messageInput);
    messageInputLayout->addWidget(sendMessageButton);

    startCallButton = new QPushButton("📞", chatsTab);
    stopCallButton = new QPushButton("★", chatsTab);
    stopCallButton->setEnabled(false);
    voipStatusLabel = new QLabel("VoIP: Disconnected", chatsTab);

    QHBoxLayout *voipLayout = new QHBoxLayout();
    voipLayout->addWidget(startCallButton);
    voipLayout->addWidget(stopCallButton);
    voipLayout->addWidget(voipStatusLabel);

    chatsLayout->addWidget(messageScroll, 3);
    chatsLayout->addLayout(messageInputLayout);
    chatsLayout->addLayout(voipLayout);
    chatsTab->setLayout(chatsLayout);

    // Вкладка 2: Друзья
    QWidget *friendsTab = new QWidget();
    QVBoxLayout *friendsLayout = new QVBoxLayout(friendsTab);
    friendsLayout->setSpacing(10);

    friendsList = new QListWidget(friendsTab);
    friendsList->setMinimumWidth(200);
    QScrollArea *friendsScroll = new QScrollArea(friendsTab);
    friendsScroll->setWidget(friendsList);
    friendsScroll->setWidgetResizable(true);
    friendsScroll->setFixedWidth(220);

    deleteFriendButton = new QPushButton("✖", friendsTab);

    friendsLayout->addWidget(friendsScroll, 1);
    friendsLayout->addWidget(deleteFriendButton);
    friendsTab->setLayout(friendsLayout);

    // Вкладка 3: Запросы на дружбу
    QWidget *friendRequestsTab = new QWidget();
    QVBoxLayout *friendRequestsLayout = new QVBoxLayout(friendRequestsTab);
    friendRequestsLayout->setSpacing(10);

    friendRequestsList = new QListWidget(friendRequestsTab);
    friendRequestsList->setMinimumWidth(200);
    QScrollArea *friendRequestsScroll = new QScrollArea(friendRequestsTab);
    friendRequestsScroll->setWidget(friendRequestsList);
    friendRequestsScroll->setWidgetResizable(true);
    friendRequestsScroll->setFixedWidth(220);

    acceptFriendButton = new QPushButton("✔", friendRequestsTab);
    rejectFriendButton = new QPushButton("✖", friendRequestsTab);

    QHBoxLayout *friendRequestActionsLayout = new QHBoxLayout();
    friendRequestActionsLayout->addWidget(acceptFriendButton);
    friendRequestActionsLayout->addWidget(rejectFriendButton);

    friendRequestsLayout->addWidget(friendRequestsScroll, 1);
    friendRequestsLayout->addLayout(friendRequestActionsLayout);
    friendRequestsTab->setLayout(friendRequestsLayout);

    // Вкладка 4: Поиск пользователей
    QWidget *searchTab = new QWidget();
    QVBoxLayout *searchLayout = new QVBoxLayout(searchTab);
    searchLayout->setSpacing(10);

    searchUserInput = new QLineEdit(searchTab);
    searchUserInput->setPlaceholderText("Поиск пользователей...");
    searchUsersButton = new QPushButton("🔍", searchTab);

    QHBoxLayout *searchInputLayout = new QHBoxLayout();
    searchInputLayout->addWidget(searchUserInput);
    searchInputLayout->addWidget(searchUsersButton);

    searchResultsList = new QListWidget(searchTab);
    searchResultsList->setMinimumWidth(200);
    QScrollArea *searchResultsScroll = new QScrollArea(searchTab);
    searchResultsScroll->setWidget(searchResultsList);
    searchResultsScroll->setWidgetResizable(true);
    searchResultsScroll->setFixedWidth(220);

    addFriendButton = new QPushButton("➕", searchTab);

    searchLayout->addLayout(searchInputLayout);
    searchLayout->addWidget(searchResultsScroll, 1);
    searchLayout->addWidget(addFriendButton);
    searchTab->setLayout(searchLayout);

    // Вкладка 5: Создание чата
    QWidget *createChatTab = new QWidget();
    QVBoxLayout *createChatLayout = new QVBoxLayout(createChatTab);
    createChatLayout->setSpacing(10);

    chatNameInput = new QLineEdit(createChatTab);
    chatNameInput->setPlaceholderText("Имя нового чата");
    createChatButton = new QPushButton("✚", createChatTab);
    inviteToChatButton = new QPushButton("👥", createChatTab);
    deleteFromChatButton = new QPushButton("✖", createChatTab);
    deleteVoiceChatButton = new QPushButton("📞✖", createChatTab);

    QHBoxLayout *chatActionsLayout = new QHBoxLayout();
    chatActionsLayout->addWidget(chatNameInput);
    chatActionsLayout->addWidget(createChatButton);
    chatActionsLayout->addWidget(inviteToChatButton);
    chatActionsLayout->addWidget(deleteFromChatButton);
    chatActionsLayout->addWidget(deleteVoiceChatButton);

    createChatLayout->addLayout(chatActionsLayout);
    createChatTab->setLayout(createChatLayout);

    // Вкладка 6: Настройки
    QWidget *accountTab = new QWidget();
    QVBoxLayout *accountLayout = new QVBoxLayout(accountTab);
    accountLayout->setSpacing(10);

    newNameInput = new QLineEdit(accountTab);
    newNameInput->setPlaceholderText("Новое имя");
    newPasswordInput = new QLineEdit(accountTab);
    newPasswordInput->setPlaceholderText("Новый пароль");
    newPasswordInput->setEchoMode(QLineEdit::Password);
    updateAccountButton = new QPushButton("Обновить аккаунт", accountTab);

    accountLayout->addWidget(new QLabel("Настройки аккаунта", accountTab));
    accountLayout->addWidget(newNameInput);
    accountLayout->addWidget(newPasswordInput);
    accountLayout->addWidget(updateAccountButton);
    accountLayout->addStretch();
    accountTab->setLayout(accountLayout);

    // Добавляем вкладки в mainTabs
    mainTabs->addTab(chatsTab, "Чаты");
    mainTabs->addTab(friendsTab, "Друзья");
    mainTabs->addTab(friendRequestsTab, "Запросы");
    mainTabs->addTab(searchTab, "Поиск");
    mainTabs->addTab(createChatTab, "Создание");
    mainTabs->addTab(accountTab, "Настройки");

    // Общие элементы интерфейса
    connectionStatusLabel = new QLabel("Отключено", mainArea);
    deleteAccountButton = new QPushButton("Удалить аккаунт", mainArea);

    QHBoxLayout *statusLayout = new QHBoxLayout();
    statusLayout->addWidget(connectionStatusLabel);
    statusLayout->addWidget(deleteAccountButton);

    mainAreaLayout->addWidget(mainTabs);
    mainAreaLayout->addLayout(statusLayout);
    mainArea->setLayout(mainAreaLayout);

    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(mainArea);
    chatWidget->setLayout(mainLayout);

    setCentralWidget(authWidget);

    // Стиль
    setStyleSheet(R"(
        QMainWindow {
            background-color: #2C2F33;
        }
        QWidget {
            background-color: #2C2F33;
            color: #FFFFFF;
            font-family: 'Whitney', 'Helvetica Neue', Helvetica, Arial, sans-serif;
            font-size: 14px;
        }
        QTabWidget::pane {
            border: none;
            background-color: #23272A;
            border-radius: 5px;
        }
        QTabBar::tab {
            background-color: #2C2F33;
            color: #B9BBBE;
            padding: 10px 15px;
            border: none;
            border-bottom: 2px solid transparent;
        }
        QTabBar::tab:selected {
            background-color: #2C2F33;
            color: #FFFFFF;
            border-bottom: 2px solid #7289DA;
        }
        QTabBar::tab:hover {
            background-color: #32353B;
            color: #FFFFFF;
        }
        QLineEdit {
            background-color: #40444B;
            color: #FFFFFF;
            border: 1px solid #40444B;
            border-radius: 3px;
            padding: 5px;
        }
        QLineEdit:focus {
            border: 1px solid #7289DA;
        }
        QTextEdit {
            background-color: #40444B;
            color: #FFFFFF;
            border: 1px solid #40444B;
            border-radius: 3px;
            padding: 5px;
            font-family: 'Whitney', 'Helvetica Neue', Helvetica, Arial, sans-serif;
            font-size: 12px;
        }
        QListWidget {
            background-color: #2C2F33;
            color: #FFFFFF;
            border: none;
            border-radius: 3px;
        }
        QListWidget::item {
            padding: 8px;
            background-color: #2C2F33;
            color: #FFFFFF;
        }
        QListWidget::item:selected {
            background-color: #40444B;
            color: #FFFFFF;
        }
        QPushButton {
            background-color: #7289DA;
            color: #FFFFFF;
            border: none;
            border-radius: 3px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background-color: #677BC4;
        }
        QPushButton:pressed {
            background-color: #5A67B3;
        }
        QPushButton:disabled {
            background-color: #5A5F6B;
            color: #B9BBBE;
        }
        QScrollArea {
            background-color: #2C2F33;
            border: none;
        }
        QLabel {
            color: #FFFFFF;
            padding: 5px;
        }
        QScrollArea QWidget QWidget {
            background-color: #2C2F33;
        }
    )");

    // Подключения сигналов
    connect(messageInput, &QLineEdit::returnPressed, this, &ChatClient::onSendMessageButtonClicked);
    connect(webSocket, &QWebSocket::connected, this, &ChatClient::onConnected);
    connect(webSocket, &QWebSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(webSocket, &QWebSocket::textMessageReceived, this, &ChatClient::onTextMessageReceived);
    connect(registerButton, &QPushButton::clicked, this, &ChatClient::onRegisterButtonClicked);
    connect(loginButton, &QPushButton::clicked, this, &ChatClient::onLoginButtonClicked);
    connect(updateAccountButton, &QPushButton::clicked, this, &ChatClient::onUpdateAccountButtonClicked);
    connect(logoutButton, &QPushButton::clicked, this, &ChatClient::onLogoutButtonClicked);
    connect(chatListWidget, &QListWidget::itemClicked, this, &ChatClient::onChatSelected);
    connect(sendMessageButton, &QPushButton::clicked, this, &ChatClient::onSendMessageButtonClicked);
    connect(createChatButton, &QPushButton::clicked, this, &ChatClient::onCreateChatButtonClicked);
    connect(inviteToChatButton, &QPushButton::clicked, this, &ChatClient::onInviteToChatButtonClicked);
    connect(deleteFromChatButton, &QPushButton::clicked, this, &ChatClient::onDeleteFromChatButtonClicked);
    connect(deleteAccountButton, &QPushButton::clicked, this, &ChatClient::onDeleteAccountButtonClicked);
    connect(deleteVoiceChatButton, &QPushButton::clicked, this, &ChatClient::onDeleteVoiceChatButtonClicked);
    connect(startCallButton, &QPushButton::clicked, this, &ChatClient::onStartCallButtonClicked);
    connect(stopCallButton, &QPushButton::clicked, this, &ChatClient::onStopCallButtonClicked);
    connect(searchUsersButton, &QPushButton::clicked, this, &ChatClient::onSearchUsersButtonClicked);
    connect(addFriendButton, &QPushButton::clicked, this, &ChatClient::onAddFriendButtonClicked);
    connect(friendsList, &QListWidget::itemClicked, this, &ChatClient::onFriendClicked);
    connect(friendsList, &QListWidget::itemDoubleClicked, this, &ChatClient::onFriendDoubleClicked);
    connect(deleteFriendButton, &QPushButton::clicked, this, &ChatClient::onDeleteFriendButtonClicked);
    connect(acceptFriendButton, &QPushButton::clicked, this, &ChatClient::onAcceptFriendRequestClicked);
    connect(rejectFriendButton, &QPushButton::clicked, this, &ChatClient::onRejectFriendRequestClicked);

    // Инициализация данных после старта
    if (isAuthenticated) {
        showChatScreen();
        requestFriendsList();
        requestChatList();
    }
}

void ChatClient::requestChatList() {
    QJsonObject message;
    message["topic"] = 2;
    message["ty"] = 2;
    message["user_id"] = currentUserId;
    sendJsonMessage(message);
    qDebug() << "Запрос списка чатов отправлен";
}

void ChatClient::showAuthScreen()
{
    setCentralWidget(authWidget);
    authWidget->show();
    chatWidget->hide();
    isAuthenticated = false;
}

void ChatClient::showChatScreen()
{
    static bool isShowing = false;
    if (isShowing) {
        qDebug() << "showChatScreen already in progress, skipping";
        return;
    }
    isShowing = true;

    setCentralWidget(chatWidget);
    chatWidget->show();
    authWidget->hide();
    isAuthenticated = true;

    QSettings settings("MyApp", "ChatClient");
    currentChatId = settings.value("currentChatId", -1).toInt();

    requestChatList();
    QTimer::singleShot(100, this, [this]() {
        if (currentChatId != -1) {
            bool chatFound = false;
            for (int i = 0; i < chatListWidget->count(); ++i) {
                QListWidgetItem *item = chatListWidget->item(i);
                if (item->data(Qt::UserRole).toInt() == currentChatId) {
                    chatListWidget->setCurrentItem(item);
                    onChatSelected(item);
                    chatFound = true;
                    break;
                }
            }
            if (!chatFound) {
                qDebug() << "Чат с ID" << currentChatId << "не найден в списке";
                currentChatId = -1;
                messageDisplay->clear();
            }
        }
        isShowing = false;
        isInitialSubscription = false; 
    });

    requestFriendsList();
}

void ChatClient::sendJsonMessage(const QJsonObject &message)
{
    QJsonDocument doc(message);
    QString jsonString = QString(doc.toJson(QJsonDocument::Compact));
    qDebug() << "Отправка сообщения:" << jsonString;
    webSocket->sendTextMessage(jsonString);
}

void ChatClient::validateInput(const QString &login, const QString &password, const QString &name)
{
    if (login.isEmpty() || password.isEmpty() || (!name.isEmpty() && name.trimmed().isEmpty())) {
        throw std::invalid_argument("All fields must be filled");
    }
    if (login.contains(" ") || password.contains(" ") || (!name.isEmpty() && name.contains(" "))) {
        throw std::invalid_argument("Fields must not contain spaces");
    }
}

void ChatClient::onRegisterButtonClicked()
{
    try {
        validateInput(loginEdit->text(), passwordEdit->text(), nameEdit->text());
        QUrlQuery query;
        query.addQueryItem("login_reg", loginEdit->text());
        query.addQueryItem("password", passwordEdit->text());
        query.addQueryItem("name", nameEdit->text());
        QUrl url(serverUrl);
        url.setQuery(query);
        webSocket->open(url);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void ChatClient::onLoginButtonClicked()
{
    try {
        validateInput(loginEdit->text(), passwordEdit->text());
        savedLogin = loginEdit->text();
        savedPassword = passwordEdit->text();
        QUrlQuery query;
        query.addQueryItem("login", savedLogin);
        query.addQueryItem("password", savedPassword);
        QUrl url(serverUrl);
        url.setQuery(query);
        webSocket->open(url);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void ChatClient::onDisconnected()
{
    qDebug() << "Disconnected from chat server";
    isConnected = false;
    connectionStatusLabel->setText("Chat: Disconnected");
    connectionStatusLabel->setStyleSheet("color: red;");
    if (isAuthenticated) {
        reconnectTimer->start();
    }
}

void ChatClient::onTextMessageReceived(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) {
        qDebug() << "JSON parsing error:" << message;
        return;
    }
    QJsonObject obj = doc.object();
    int topic = obj["topic"].toInt();
    qDebug() << "Received message with topic:" << topic << "full message:" << message;

    switch (topic) {
    case 0:
        break;
    case 1:
        break;
    case 2: // GetChatList
        updateChatList(obj["chats"].toArray());
        break;
    case 4: // CreateChat
    {
        if (!obj.contains("chat_id") || !obj.contains("chat_name")) {
            qDebug() << "Invalid CreateChat message: missing chat_id or chat_name";
            break;
        }
        int chatId = obj["chat_id"].toInt(-1);
        QString chatName = obj["chat_name"].toString();
        bool isVoiceChat = obj["isVoiceChat"].toBool(false);
        if (chatId == -1) {
            qDebug() << "Invalid chat_id in CreateChat message";
            break;
        }
        QMetaObject::invokeMethod(this, [this, chatId, chatName, isVoiceChat]() {
            bool chatExists = false;
            for (int i = 0; i < chatListWidget->count(); ++i) {
                QListWidgetItem* item = chatListWidget->item(i);
                if (item->data(Qt::UserRole).toInt() == chatId) {
                    chatExists = true;
                    item->setText(chatName);
                    item->setData(Qt::UserRole + 1, isVoiceChat);
                    break;
                }
            }
            if (!chatExists) {
                QListWidgetItem* item = new QListWidgetItem(chatName, chatListWidget);
                item->setData(Qt::UserRole, chatId);
                item->setData(Qt::UserRole + 1, isVoiceChat);
                item->setSizeHint(QSize(70, 48));
                QFont font;
                font.setPointSize(10);
                item->setFont(font);
                chatListWidget->addItem(item);

                currentChatId = chatId;
                onChatSelected(item);

                QJsonObject historyMessage;
                historyMessage["topic"] = 6;
                historyMessage["ty"] = 6;
                historyMessage["to"] = chatId;
                sendJsonMessage(historyMessage);

                QMessageBox::information(this, "Уведомление", QString("Чат создан: %1").arg(chatName));
            }
        }, Qt::QueuedConnection);
        break;
    }
    case 3: // NewMessage
        {
            if (obj.contains("error")) {
                QMetaObject::invokeMethod(this, [this, obj]() {
                    QMessageBox::warning(this, "Ошибка", obj["error"].toString());
                }, Qt::QueuedConnection);
                break;
            }
            qDebug() << "Получено NewMessage:" << obj;
            if (!obj.contains("user_name") || !obj.contains("text") || !obj.contains("date") || !obj.contains("msg_id")) {
                qDebug() << "Неверный формат NewMessage, отсутствуют поля:" << obj;
                break;
            }
            int msgId = obj["msg_id"].toInt();
            int chatId = obj.contains("to") ? obj["to"].toInt(-1) : currentChatId;
            if (chatId != currentChatId) {
                qDebug() << "Сообщение с msg_id" << msgId << "для chatId" << chatId << ", текущий чат" << currentChatId << ", пропуск";
                break;
            }
            if (displayedMessageIds[currentChatId].contains(msgId)) {
                qDebug() << "Сообщение с msg_id" << msgId << "уже отображено в chatId" << currentChatId << ", пропуск отображения";
                break;
            }
            displayedMessageIds[currentChatId].insert(msgId);
            chatMessages[currentChatId].append(obj); // Добавляем в chatMessages
            appendMessage(obj["user_name"].toString(), obj["text"].toString(), obj["date"].toVariant().toLongLong());
            break;
        }
    case 6: // GetMessageList
        {
            if (!obj.contains("messages") || !obj["messages"].isArray()) {
                qDebug() << "No valid 'messages' array in response:" << obj;
                return;
            }
            qDebug() << "Updating message list for chat ID:" << currentChatId << "with messages:" << obj["messages"];
            QJsonArray messages = obj["messages"].toArray();
            QMetaObject::invokeMethod(this, [this, messages]() {
                QSet<int> newMessageIds;
                for (const auto &msg : messages) {
                    QJsonObject msgObj = msg.toObject();
                    if (!msgObj.contains("msg_id")) continue;
                    newMessageIds.insert(msgObj["msg_id"].toInt());
                }

                for (int i = chatMessages[currentChatId].size() - 1; i >= 0; --i) {
                    QJsonObject msgObj = chatMessages[currentChatId][i];
                    int msgId = msgObj["msg_id"].toInt();
                    if (!newMessageIds.contains(msgId)) {
                        chatMessages[currentChatId].removeAt(i);
                        displayedMessageIds[currentChatId].remove(msgId);
                    }
                }

                for (const auto &msg : messages) {
                    QJsonObject msgObj = msg.toObject();
                    if (!msgObj.contains("user_name") || !msgObj.contains("text") || !msgObj.contains("date") || !msgObj.contains("msg_id")) {
                        qDebug() << "Неверное сообщение в истории:" << msgObj;
                        continue;
                    }
                    int msgId = msgObj["msg_id"].toInt();
                    if (displayedMessageIds[currentChatId].contains(msgId)) {
                        bool found = false;
                        for (const QJsonObject &existingMsg : chatMessages[currentChatId]) {
                            if (existingMsg["msg_id"].toInt() == msgId) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            chatMessages[currentChatId].append(msgObj);
                        }
                        continue;
                    }
                    chatMessages[currentChatId].append(msgObj);
                    displayedMessageIds[currentChatId].insert(msgId);
                    appendMessage(msgObj["user_name"].toString(), msgObj["text"].toString(), msgObj["date"].toVariant().toLongLong());
                }

                const int MAX_MESSAGES = 100;
                while (chatMessages[currentChatId].size() > MAX_MESSAGES) {
                    QJsonObject oldestMsg = chatMessages[currentChatId].takeFirst();
                    displayedMessageIds[currentChatId].remove(oldestMsg["msg_id"].toInt());
                }
            }, Qt::QueuedConnection);
            break;
        }
    case 7: // GetMyId
        currentUserId = obj["user_id"].toInt();
        showChatScreen();
        break;
    case 8: // DeleteAccount
    {
        if (obj.contains("status") && obj["status"].toString() == "success") {
            QMetaObject::invokeMethod(this, [this]() {
                webSocket->close();
                currentUserId = -1;
                currentChatId = -1;
                messageDisplay->clear();
                chatListWidget->clear();
                friendsList->clear();
                friendRequestsList->clear();
                savedLogin.clear();
                savedPassword.clear();
                isAuthenticated = false;
                showAuthScreen();
                QMessageBox::information(this, "Успех", "Аккаунт успешно удалён");
                qDebug() << "Пользовательский аккаунт удалён";
            }, Qt::QueuedConnection);
        } else {
            QString errorMsg = obj.contains("error") ? obj["error"].toString() : "Не удалось удалить аккаунт";
            QMetaObject::invokeMethod(this, [this, errorMsg]() {
                QMessageBox::warning(this, "Ошибка", errorMsg);
            }, Qt::QueuedConnection);
        }
        break;
    }
    case 9: // DeleteUserFromChat
        {
            int userId = obj["user_id"].toInt(-1);
            if (userId == currentUserId) {
                QMetaObject::invokeMethod(this, [this]() {
                    for (int i = 0; i < chatListWidget->count(); ++i) {
                        QListWidgetItem* item = chatListWidget->item(i);
                        if (item->data(Qt::UserRole).toInt() == currentChatId) {
                            delete chatListWidget->takeItem(i);
                            break;
                        }
                    }
                    chatMessages.remove(currentChatId);
                    displayedMessageIds.remove(currentChatId);
                    currentChatId = -1;
                    messageDisplay->clear();
                    QMessageBox::information(this, "Уведомление", "Вы покинули чат");
                }, Qt::QueuedConnection);
            }
            break;
        }
    case 10: // InviteToChat
        {
            if (obj.contains("error")) {
                QMessageBox::warning(this, "Ошибка", obj["error"].toString());
                requestChatList();
                break;
            }
            if (obj["status"].toString() == "success") {
                QMessageBox::information(this, "Успех", "Пользователи добавлены в чат");
                requestChatList();
                break;
            }
            int chatId = obj["chat_id"].toInt(-1);
            QString chatName = obj["chat_name"].toString();
            bool isVoiceChat = obj.contains("isVoiceChat") && obj["isVoiceChat"].toBool();
            QJsonArray invited = obj["invited"].toArray();
            bool isInvited = false;
            for (const auto &id : invited) {
                if (id.toInt() == currentUserId) {
                    isInvited = true;
                    break;
                }
            }
            if (!isInvited && obj["user_id"].toInt() != currentUserId) {
                qDebug() << "Ignoring InviteToChat: user not invited";
                break;
            }
            if (chatId == -1) {
                qDebug() << "Invalid chat_id in InviteToChat message";
                break;
            }
            QMetaObject::invokeMethod(this, [this, chatId, chatName, isVoiceChat]() {
                bool chatExists = false;
                for (int i = 0; i < chatListWidget->count(); ++i) {
                    QListWidgetItem* item = chatListWidget->item(i);
                    if (item->data(Qt::UserRole).toInt() == chatId) {
                        chatExists = true;
                        item->setText(chatName);
                        item->setData(Qt::UserRole + 1, isVoiceChat);
                        break;
                    }
                }
                if (!chatExists) {
                    QListWidgetItem* item = new QListWidgetItem(chatName, chatListWidget);
                    item->setData(Qt::UserRole, chatId);
                    item->setData(Qt::UserRole + 1, isVoiceChat);
                    item->setSizeHint(QSize(70, 48));
                    QFont font;
                    font.setPointSize(10);
                    item->setFont(font);
                    chatListWidget->addItem(item);
                    QMessageBox::information(this, "Уведомление", QString("Вы были добавлены в чат: %1").arg(chatName));
                }
                requestChatList();
                QJsonObject getUsersMessage;
                getUsersMessage["topic"] = 11;
                getUsersMessage["ty"] = 11;
                getUsersMessage["to"] = chatId;
                sendJsonMessage(getUsersMessage);
            }, Qt::QueuedConnection);
            break;
        }
    case 11: // GetUserInChatList
    {
        if (!obj.contains("users") || !obj["users"].isArray()) {
            qDebug() << "Invalid GetUserInChatList message: missing users array";
            break;
        }
        QJsonArray users = obj["users"].toArray();
        QMetaObject::invokeMethod(this, [this, users]() {
            qDebug() << "Users in chat ID" << currentChatId << ":" << users;
        }, Qt::QueuedConnection);
        break;
    }
    case 12: // SearchUsersByName
        if (!obj.contains("users")) {
            qDebug() << "Неверное сообщение SearchUsersByName: отсутствует массив users";
            break;
        }
        onSearchResultReceived(obj["users"].toArray());
        break;
    case 13: // AddFriend
            {
                if (obj.contains("error")) {
                    QString errorMsg = obj["error"].toString();
                    qDebug() << "Friend request error:" << errorMsg;
                    QMetaObject::invokeMethod(this, [this, errorMsg]() {
                        QMessageBox::warning(this, "Ошибка", errorMsg);
                    }, Qt::QueuedConnection);
                    break;
                }
                if (obj["status"].toString() == "success") {
                    QMetaObject::invokeMethod(this, [this]() {
                        QMessageBox::information(this, "Успех", "Запрос на дружбу отправлен");
                        requestFriendsList(); 
                    }, Qt::QueuedConnection);
                }
                break;
            }
    case 14: // GetFriendsList
        if (!obj.contains("friends")) {
            qDebug() << "Неверное сообщение GetFriendsList: отсутствует массив friends";
            break;
        }
        onFriendsListReceived(obj["friends"].toArray());
        if (obj.contains("friend_requests") && obj["friend_requests"].isArray()) {
            onFriendRequestsReceived(obj["friend_requests"].toArray());
        }
        break;
    case 15: // AcceptFriendRequest
        if (obj["status"].toString() == "accepted") {
            QMessageBox::information(this, "Успех", "Друг принят");
            requestFriendsList();
        }
        break;
    case 17: // NewFriendRequest
        if (obj.contains("friend_id") && obj.contains("friend_name")) {
            QMetaObject::invokeMethod(this, [this, obj]() {
                int friendId = obj["friend_id"].toInt();
                QString friendName = obj["friend_name"].toString();
                QListWidgetItem *item = new QListWidgetItem(friendName, friendRequestsList);
                item->setData(Qt::UserRole, friendId);
                this->acceptFriendButton->setEnabled(true);
                this->rejectFriendButton->setEnabled(true);
                QMessageBox::information(this, "Новое уведомление", QString("Новая заявка от %1").arg(friendName));
            }, Qt::QueuedConnection);
        }
        break;
    case 20: // UpdateAccount
        if (obj["status"].toString() == "success") {
            savedLogin = obj.value("name").toString(savedLogin);
            QMessageBox::information(this, "Success", "Account updated successfully");
        } else {
            QMessageBox::warning(this, "Error", obj["error"].toString());
        }
        break;
    case 21: // DeleteVoiceChat
        if (obj["status"].toString() == "success") {
            int chatId = obj["chat_id"].toInt();
            if (chatId == currentChatId) {
                stop();
                voipStatusLabel->setText("VoIP: Disconnected");
                voipStatusLabel->setStyleSheet("color: red;");
                startCallButton->setEnabled(true);
                stopCallButton->setEnabled(false);
            }
            for (int i = 0; i < chatListWidget->count(); ++i) {
                QListWidgetItem* item = chatListWidget->item(i);
                if (item->data(Qt::UserRole).toInt() == chatId) {
                    delete chatListWidget->takeItem(i);
                    break;
                }
            }
            QMessageBox::information(this, "Success", "Voice chat deleted");
        } else {
            QMessageBox::warning(this, "Error", obj["error"].toString());
        }
        break;
    case 18: // DeleteFriend
            {
                if (obj.contains("error")) {
                    QString errorMsg = obj["error"].toString();
                    qDebug() << "Friend deletion error:" << errorMsg;
                    QMetaObject::invokeMethod(this, [this, errorMsg]() {
                        QMessageBox::warning(this, "Ошибка", errorMsg);
                    }, Qt::QueuedConnection);
                    break;
                }
                if (obj["status"].toString() == "success") {
                    QMetaObject::invokeMethod(this, [this]() {
                        QMessageBox::information(this, "Успех", "Друг удален");
                        requestFriendsList();
                    }, Qt::QueuedConnection);
                }
                break;
            }
    case 22: // Logout
        if (obj["status"].toString() == "success") {
            QMetaObject::invokeMethod(this, [this]() {
                webSocket->close();
                currentUserId = -1;
                currentChatId = -1;
                messageDisplay->clear();
                chatListWidget->clear();
                friendsList->clear();
                friendRequestsList->clear();
                savedLogin.clear();
                savedPassword.clear();
                isAuthenticated = false;
                showAuthScreen();
                qDebug() << "Пользователь вышел";
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, obj]() {
                QMessageBox::warning(this, "Ошибка", obj["error"].toString());
            }, Qt::QueuedConnection);
        }
        break;
    default:
        handleErrorMessage("Unknown topic: " + QString::number(topic));
        break;
    }
}

void ChatClient::onAcceptFriendRequestClicked()
{
    QListWidgetItem *item = friendRequestsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Ошибка", "Выберите заявку для принятия");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1) {
        QMessageBox::warning(this, "Ошибка", "Неверный идентификатор друга");
        return;
    }

    QJsonObject message;
    message["topic"] = 15;
    message["ty"] = 15;
    message["user_id"] = currentUserId;
    message["friend_id"] = friendId;
    sendJsonMessage(message);

    qDebug() << "Отправлена заявка на принятие друга:" << friendId;
    friendRequestsList->takeItem(friendRequestsList->currentRow());
    acceptFriendButton->setEnabled(false);
    rejectFriendButton->setEnabled(false);

    requestFriendsList();
}

void ChatClient::onRejectFriendRequestClicked()
{
    QListWidgetItem *item = friendRequestsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Ошибка", "Выберите заявку для отклонения");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1) {
        QMessageBox::warning(this, "Ошибка", "Неверный идентификатор друга");
        return;
    }

    QJsonObject message;
    message["topic"] = 16;
    message["ty"] = 16;
    message["user_id"] = currentUserId;
    message["friend_id"] = friendId;
    sendJsonMessage(message);

    qDebug() << "Отправлена заявка на отклонение друга:" << friendId;
    friendRequestsList->takeItem(friendRequestsList->currentRow());
    acceptFriendButton->setEnabled(false);
    rejectFriendButton->setEnabled(false);

    requestFriendsList();
}

void ChatClient::updateChatList(const QJsonArray &chats)
{
    QMetaObject::invokeMethod(this, [this, chats]() {
        chatListWidget->clear();
        qDebug() << "Обновление списка чатов, получено:" << chats.size();
        for (const auto &chat : chats) {
            QJsonObject chatObj = chat.toObject();
            int chatId = chatObj["chat_id"].toInt();
            QString chatName = chatObj["chat_name"].toString();
            bool isVoiceChat = chatObj["isVoiceChat"].toBool(false);

            QString lastMessage = lastMessages.value(chatId, "Нет сообщений");
            QListWidgetItem *item = new QListWidgetItem();
            item->setData(Qt::UserRole, chatId);
            item->setData(Qt::UserRole + 1, isVoiceChat);
            item->setText(chatName + "\n" + lastMessage);
            item->setSizeHint(QSize(70, 48));

            QFont font;
            font.setPointSize(10);
            item->setFont(font);
            chatListWidget->addItem(item);

            qDebug() << "Чат: ID =" << chatId << "Имя =" << chatName << "Голосовой =" << isVoiceChat;
        }
        if (chats.isEmpty()) {
            qDebug() << "Чаты не получены от сервера";
        }
    }, Qt::QueuedConnection);
}

void ChatClient::updateMessageList(const QJsonArray &messages) {
    const int MAX_MESSAGES = 100;
    QMetaObject::invokeMethod(this, [this, messages]() {
        qDebug() << "Обработка списка сообщений для чата с ID:" << currentChatId << ", количество сообщений:" << messages.size();

        for (const auto &msg : messages) {
            if (!msg.isObject()) continue;
            QJsonObject msgObj = msg.toObject();
            if (!msgObj.contains("user_name") || !msgObj.contains("text") || !msgObj.contains("date") || !msgObj.contains("msg_id")) continue;
            int msgId = msgObj["msg_id"].toInt();
            if (displayedMessageIds[currentChatId].contains(msgId)) continue;

            chatMessages[currentChatId].append(msgObj);
            displayedMessageIds[currentChatId].insert(msgId);
            appendMessage(msgObj["user_name"].toString(), msgObj["text"].toString(), msgObj["date"].toVariant().toLongLong());

            if (chatMessages[currentChatId].size() > MAX_MESSAGES) {
                QJsonObject oldestMsg = chatMessages[currentChatId].takeFirst();
                displayedMessageIds[currentChatId].remove(oldestMsg["msg_id"].toInt());
            }
        }
    }, Qt::QueuedConnection);
}

void ChatClient::appendMessage(const QString &userName, const QString &text, qint64 date) {
    QMetaObject::invokeMethod(this, [this, userName, text, date]() {
        qDebug() << "Добавление сообщения от" << userName << "с текстом:" << text << "в" << date;
        QDateTime dateTime;
        dateTime.setMSecsSinceEpoch(date);
        QString formattedMessage = QString("<span style='color: #FFFFFF; font-weight: bold;'>%1</span> "
                                          "<span style='color: #99AAB5;'>(%2)</span>: "
                                          "<span style='color: #B9BBBE;'>%3</span><br>")
            .arg(userName)
            .arg(dateTime.toString("hh:mm:ss"))
            .arg(text);

        messageDisplay->append(formattedMessage);

        if (currentChatId != -1) {
            lastMessages[currentChatId] = QString("%1: %2").arg(userName).arg(text);
        }
        QScrollBar *scrollBar = messageDisplay->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }, Qt::QueuedConnection);
}

void ChatClient::handleErrorMessage(const QString &message)
{
    QMessageBox::warning(this, "Error", message);
}

void ChatClient::onChatSelected(QListWidgetItem *item) {
    if (!item) return;

    int newChatId = item->data(Qt::UserRole).toInt();

    if (currentChatId != -1 && currentChatId != newChatId) {
        QJsonObject unsubscribeMessage;
        unsubscribeMessage["topic"] = 2;
        unsubscribeMessage["ty"] = 2;
        sendJsonMessage(unsubscribeMessage);
        qDebug() << "Отписались от чата с ID:" << currentChatId;
    }

    currentChatId = newChatId;
    messageDisplay->clear();

    // Отображаем существующие сообщения из chatMessages
    if (chatMessages.contains(currentChatId)) {
        for (const QJsonObject &msgObj : chatMessages[currentChatId]) {
            if (!msgObj.contains("user_name") || !msgObj.contains("text") || !msgObj.contains("date") || !msgObj.contains("msg_id")) {
                qDebug() << "Неверное сообщение в локальном хранилище:" << msgObj;
                continue;
            }
            int msgId = msgObj["msg_id"].toInt();
            if (!displayedMessageIds[currentChatId].contains(msgId)) {
                displayedMessageIds[currentChatId].insert(msgId);
            }
            appendMessage(msgObj["user_name"].toString(), msgObj["text"].toString(), msgObj["date"].toVariant().toLongLong());
        }
    }

    QJsonObject subscribeMessage;
    subscribeMessage["topic"] = 1;
    subscribeMessage["ty"] = 1;
    subscribeMessage["to"] = currentChatId;
    sendJsonMessage(subscribeMessage);
    qDebug() << "Подписались на чат с ID:" << currentChatId;

    QJsonObject historyMessage;
    historyMessage["topic"] = 6;
    historyMessage["ty"] = 6;
    historyMessage["to"] = currentChatId;
    sendJsonMessage(historyMessage);
    qDebug() << "Запросили историю сообщений для чата с ID:" << currentChatId;

    QSettings settings("MyApp", "ChatClient");
    settings.setValue("currentChatId", currentChatId);
    messageInput->clear();
    messageDisplay->verticalScrollBar()->setValue(messageDisplay->verticalScrollBar()->maximum());
}

void ChatClient::onSendMessageButtonClicked() {
    QString messageText = messageInput->text().trimmed();
    if (messageText.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Сообщение не может быть пустым");
        return;
    }

    if (currentChatId == -1) {
        QMessageBox::warning(this, "Ошибка", "Выберите чат для отправки сообщения");
        return;
    }

    bool chatExists = false;
    for (int i = 0; i < chatListWidget->count(); ++i) {
        if (chatListWidget->item(i)->data(Qt::UserRole).toInt() == currentChatId) {
            chatExists = true;
            break;
        }
    }
    if (!chatExists) {
        QMessageBox::warning(this, "Ошибка", "Выбранный чат больше не существует");
        currentChatId = -1;
        messageDisplay->clear();
        return;
    }

    QJsonObject message;
    message["topic"] = 3;
    message["ty"] = 3;
    message["to"] = currentChatId;
    message["msg"] = messageText;
    message["user_id"] = currentUserId;
    sendJsonMessage(message);

    messageInput->clear();
    messageInput->setFocus();
    qDebug() << "Сообщение отправлено в chatId:" << currentChatId << "с текстом:" << messageText;
}

void ChatClient::onUpdateAccountButtonClicked()
{
    QString newName = newNameInput->text().trimmed();
    QString newPassword = newPasswordInput->text().trimmed();

    if (newName.isEmpty() && newPassword.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Заполните хотя бы одно поле");
        return;
    }
    if (!newName.isEmpty() && newName.contains(" ")) {
        QMessageBox::warning(this, "Ошибка", "Имя не должно содержать пробелы");
        return;
    }
    if (!newPassword.isEmpty() && newPassword.contains(" ")) {
        QMessageBox::warning(this, "Ошибка", "Пароль не должен содержать пробелы");
        return;
    }

    QJsonObject updateObj;
    updateObj["topic"] = 20;
    updateObj["ty"] = 20;
    updateObj["user_id"] = currentUserId;
    if (!newName.isEmpty()) updateObj["name"] = newName;
    if (!newPassword.isEmpty()) updateObj["password"] = newPassword;

    sendJsonMessage(updateObj);
    newNameInput->clear();
    newPasswordInput->clear();
}


void ChatClient::onDeleteVoiceChatButtonClicked()
{
    if (currentChatId == -1 || !chatListWidget->currentItem() || !chatListWidget->currentItem()->data(Qt::UserRole + 1).toBool()) {
        QMessageBox::warning(this, "Error", "Select a voice chat to delete");
        return;
    }

    QJsonObject deleteObj;
    deleteObj["ty"] = 21;
    deleteObj["chat_id"] = currentChatId;
    deleteObj["user_id"] = currentUserId;
    sendJsonMessage(deleteObj);
}

void ChatClient::onCreateChatButtonClicked()
{
    QString chatName = chatNameInput->text().trimmed();
    if (chatName.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Имя чата не может быть пустым");
        return;
    }
    bool isVoiceChat = QMessageBox::question(this, "Тип чата", "Создать голосовой чат?") == QMessageBox::Yes;
    QJsonObject message;
    message["topic"] = 4;
    message["ty"] = 4;
    message["chatName"] = chatName;
    message["Invited"] = QJsonArray();
    message["isVoiceChat"] = isVoiceChat;
    sendJsonMessage(message);
    chatNameInput->clear();
    qDebug() << "Отправлен запрос на создание чата:" << chatName << (isVoiceChat ? "(голосовой)" : "(текстовый)");
}


void ChatClient::onInviteToChatButtonClicked() {
    if (currentChatId == -1) {
        QMessageBox::warning(this, "Error", "Сначала выберите чат");
        return;
    }

    QListWidgetItem *item = friendsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Error", "Выберите друга для приглашения в чат");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1 || friendId == currentUserId) {
        QMessageBox::warning(this, "Error", "Неверный друг или нельзя пригласить самого себя");
        return;
    }

    static QTime lastInviteTime = QTime::currentTime();
    if (lastInviteTime.msecsTo(QTime::currentTime()) < 1000) {
        qDebug() << "Ignoring duplicate invite within 1000ms";
        return;
    }
    lastInviteTime = QTime::currentTime();

    bool isVoiceChat = false;
    for (int i = 0; i < chatListWidget->count(); ++i) {
        QListWidgetItem *chatItem = chatListWidget->item(i);
        if (chatItem->data(Qt::UserRole).toInt() == currentChatId) {
            isVoiceChat = chatItem->data(Qt::UserRole + 1).toBool();
            break;
        }
    }

    QJsonObject message;
    message["topic"] = 10;
    message["ty"] = 10;
    QJsonArray invited;
    invited.append(friendId);
    message["Invited"] = invited;
    message["chatId"] = currentChatId;
    message["isVoiceChat"] = isVoiceChat;
    sendJsonMessage(message);
    qDebug() << "Sent invite for friend ID:" << friendId << "to chat ID:" << currentChatId << "isVoiceChat:" << isVoiceChat;

    QJsonObject getUsersMessage;
    getUsersMessage["topic"] = 11;
    getUsersMessage["ty"] = 11;
    getUsersMessage["to"] = currentChatId;
    sendJsonMessage(getUsersMessage);
}

void ChatClient::onFriendDoubleClicked(QListWidgetItem *item) {
    if (!item || currentChatId == -1) {
        QMessageBox::warning(this, "Error", "Сначала выберите чат и друга");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1 || friendId == currentUserId) {
        QMessageBox::warning(this, "Error", "Неверный друг или нельзя пригласить самого себя");
        return;
    }

    static QTime lastInviteTime = QTime::currentTime();
    if (lastInviteTime.msecsTo(QTime::currentTime()) < 1000) {
        qDebug() << "Ignoring duplicate invite within 1000ms";
        return;
    }
    lastInviteTime = QTime::currentTime();

    bool isVoiceChat = false;
    for (int i = 0; i < chatListWidget->count(); ++i) {
        QListWidgetItem *chatItem = chatListWidget->item(i);
        if (chatItem->data(Qt::UserRole).toInt() == currentChatId) {
            isVoiceChat = chatItem->data(Qt::UserRole + 1).toBool();
            break;
        }
    }

    QJsonObject message;
    message["topic"] = 10;
    message["ty"] = 10;
    QJsonArray invited;
    invited.append(friendId);
    message["Invited"] = invited;
    message["chatId"] = currentChatId;
    message["isVoiceChat"] = isVoiceChat;
    sendJsonMessage(message);
    qDebug() << "Sent invite for friend ID:" << friendId << "to chat ID:" << currentChatId << "via double-click, isVoiceChat:" << isVoiceChat;

    QJsonObject getUsersMessage;
    getUsersMessage["topic"] = 11;
    getUsersMessage["ty"] = 11;
    getUsersMessage["to"] = currentChatId;
    sendJsonMessage(getUsersMessage);
}

void ChatClient::onDeleteFromChatButtonClicked()
{
    if (currentChatId == -1) {
        QMessageBox::warning(this, "Error", "Сначала выберите чат");
        return;
    }

    if (QMessageBox::question(this, "Подтверждение", "Вы уверены, что хотите покинуть этот чат?") != QMessageBox::Yes) {
        return;
    }

    QJsonObject message;
    message["topic"] = 7;
    message["ty"] = 7;
    message["chatId"] = currentChatId;
    message["userId"] = currentUserId;
    sendJsonMessage(message);

    qDebug() << "Отправлен запрос на выход из чата ID:" << currentChatId;
}

void ChatClient::onDeleteAccountButtonClicked()
{
    if (QMessageBox::question(this, "Confirm", "Are you sure you want to delete your account?") == QMessageBox::Yes) {
        QJsonObject message;
        message["topic"] = 8;
        message["ty"] = 8;
        sendJsonMessage(message);
    }
}

void ChatClient::sendSearchUsersRequest(const QString &searchTerm)
{
    if (searchTerm.isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Поле поиска не может быть пустым");
        return;
    }

    QJsonObject message;
    message["topic"] = 12;
    message["ty"] = 12;
    message["searchTerm"] = searchTerm;
    sendJsonMessage(message);
    qDebug() << "Отправлен запрос поиска для термина:" << searchTerm;
}

void ChatClient::onSearchUsersButtonClicked()
{
    QString searchTerm = searchUserInput->text().trimmed();
    sendSearchUsersRequest(searchTerm);
}

void ChatClient::onSearchResultReceived(const QJsonArray &users)
{
    QMetaObject::invokeMethod(this, [this, users]() {
        searchResultsList->clear();
        for (const auto &user : users) {
            QJsonObject userObj = user.toObject();
            int userId = userObj["user_id"].toInt();
            QString userName = userObj["user_name"].toString();
            QListWidgetItem *item = new QListWidgetItem(userName, searchResultsList);
            item->setData(Qt::UserRole, userId);
        }
        if (users.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem("Пользователи не найдены", searchResultsList);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    }, Qt::QueuedConnection);
}

void ChatClient::onAddFriendButtonClicked() {
    QListWidgetItem *item = searchResultsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Ошибка", "Выберите пользователя для добавления в друзья");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1 || friendId == currentUserId) {
        QMessageBox::warning(this, "Ошибка", "Неверный пользователь или нельзя добавить самого себя");
        return;
    }

    QJsonObject message;
    message["topic"] = 13;
    message["ty"] = 13;
    message["user_id"] = currentUserId;
    message["friend_id"] = friendId;
    sendJsonMessage(message);
    qDebug() << "Отправка сообщения: " << QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString().c_str();
    qDebug() << "Отправлен запрос на добавление друга: user_id =" << currentUserId << ", friend_id =" << friendId;
}

void ChatClient::onFriendClicked(QListWidgetItem *item) {
    if (!item || currentChatId == -1) {
        QMessageBox::warning(this, "Ошибка", "Выберите действительного друга и чат");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1 || friendId == currentUserId) {
        QMessageBox::warning(this, "Ошибка", "Неверный друг или нельзя пригласить самого себя");
        return;
    }

    static QTime lastInviteTime = QTime::currentTime();
    if (lastInviteTime.msecsTo(QTime::currentTime()) < 1000) {
        qDebug() << "Ignoring duplicate invite within 1000ms";
        return;
    }
    lastInviteTime = QTime::currentTime();

    bool isVoiceChat = false;
    for (int i = 0; i < chatListWidget->count(); ++i) {
        QListWidgetItem *chatItem = chatListWidget->item(i);
        if (chatItem->data(Qt::UserRole).toInt() == currentChatId) {
            isVoiceChat = chatItem->data(Qt::UserRole + 1).toBool();
            break;
        }
    }

    QJsonObject message;
    message["topic"] = 10;
    message["ty"] = 10;
    QJsonArray invited;
    invited.append(friendId);
    message["Invited"] = invited;
    message["chatId"] = currentChatId;
    message["isVoiceChat"] = isVoiceChat;
    sendJsonMessage(message);
    qDebug() << "Sent invite for friend ID:" << friendId << "to chat ID:" << currentChatId << "isVoiceChat:" << isVoiceChat;
    QJsonObject getUsersMessage;
    getUsersMessage["topic"] = 11;
    getUsersMessage["ty"] = 11;
    getUsersMessage["to"] = currentChatId;
    sendJsonMessage(getUsersMessage);
}

void ChatClient::onLogoutButtonClicked()
{
    if (QMessageBox::question(this, "Подтверждение", "Выйти из аккаунта?") == QMessageBox::Yes) {
        QJsonObject message;
        message["topic"] = 22;
        message["ty"] = 22;
        message["user_id"] = currentUserId;
        sendJsonMessage(message);
        qDebug() << "Отправлен запрос на выход из аккаунта";
    }
}


void ChatClient::requestFriendsList()
{
    if (!friendsList) {
        qDebug() << "Ошибка: friendsList не инициализирован, запрос списка друзей отменён";
        return;
    }

    QJsonObject message;
    message["topic"] = 14;
    message["ty"] = 14;
    message["user_id"] = currentUserId;
    sendJsonMessage(message);
}

void ChatClient::onFriendsListReceived(const QJsonArray &friends)
{
    if (!friendsList) {
        qDebug() << "Ошибка: friendsList не инициализирован";
        return;
    }

    QMetaObject::invokeMethod(this, [this, friends]() {
        friendsList->clear();
        for (const auto &friendData : friends) {
            QJsonObject friendObj = friendData.toObject();
            if (!friendObj.contains("friend_id") || !friendObj.contains("friend_name")) {
                qDebug() << "Ошибка: Неверный формат объекта друга:" << friendObj;
                continue;
            }
            int friendId = friendObj["friend_id"].toInt();
            QString friendName = friendObj["friend_name"].toString();
            QListWidgetItem *item = new QListWidgetItem(friendName, friendsList);
            item->setData(Qt::UserRole, friendId);
        }
        if (friends.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem("Друзей нет", friendsList);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    }, Qt::QueuedConnection);
}

void ChatClient::onFriendRequestsReceived(const QJsonArray &requests)
{
    if (!friendRequestsList) {
        qDebug() << "Ошибка: friendRequestsList не инициализирован";
        return;
    }

    QMetaObject::invokeMethod(this, [this, requests]() {
        friendRequestsList->clear();
        for (const auto &request : requests) {
            QJsonObject requestObj = request.toObject();
            if (!requestObj.contains("friend_id") || !requestObj.contains("friend_name")) {
                qDebug() << "Ошибка: Неверный формат заявки в друзья:" << requestObj;
                continue;
            }
            int friendId = requestObj["friend_id"].toInt();
            QString friendName = requestObj["friend_name"].toString();
            QListWidgetItem *item = new QListWidgetItem(friendName, friendRequestsList);
            item->setData(Qt::UserRole, friendId);
        }
        if (requests.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem("Заявок нет", friendRequestsList);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    }, Qt::QueuedConnection);
}

void ChatClient::onDeleteFriendButtonClicked() {
    QListWidgetItem *item = friendsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Ошибка", "Выберите друга для удаления");
        return;
    }

    int friendId = item->data(Qt::UserRole).toInt();
    if (friendId == -1 || friendId == currentUserId) {
        QMessageBox::warning(this, "Ошибка", "Неверный пользователь");
        return;
    }

    QJsonObject message;
    message["topic"] = 18;
    message["ty"] = 18;
    message["user_id"] = currentUserId;
    message["friend_id"] = friendId;
    sendJsonMessage(message);
    qDebug() << "Отправка сообщения: " << QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString().c_str();
    qDebug() << "Отправлен запрос на удаление друга ID:" << friendId;
}

bool ChatClient::checkConnection(const QString &channel_id)
{
    (void)channel_id;
    qDebug() << "Checking VoIP server connection...";
    try {
        socket_->send_to(boost::asio::buffer("PING"), endpoint_);
        socket_->non_blocking(true);
        std::array<char, 4> recv_buf;
        udp::endpoint sender_endpoint;
        bool received = false;

        auto start = std::chrono::steady_clock::now();
        while (!received && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
            boost::system::error_code ec;
            size_t bytes_recvd = socket_->receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);
            if (!ec && bytes_recvd == 4 && std::string(recv_buf.data(), 4) == "PONG") {
                received = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        socket_->non_blocking(false);

        if (received) {
            qDebug() << "VoIP server connection verified";
            voipStatusLabel->setText("VoIP: Connected");
            voipStatusLabel->setStyleSheet("color: green;");
            return true;
        }
        qDebug() << "VoIP connection timeout";
        voipStatusLabel->setText("VoIP: Disconnected");
        voipStatusLabel->setStyleSheet("color: red;");
        return false;
    } catch (std::exception &e) {
        qDebug() << "VoIP connection error:" << e.what();
        voipStatusLabel->setText("VoIP: Error");
        voipStatusLabel->setStyleSheet("color: red;");
        return false;
    }


}

bool ChatClient::registerWithServer(const QString &channel_id)
{
    qDebug() << "Registering with VoIP server...";
    try {
        std::string msg = "REGISTER " + channel_id.toStdString();
        socket_->send_to(boost::asio::buffer(msg), endpoint_);

        socket_->non_blocking(true);
        std::array<char, 13> recv_buf;
        udp::endpoint sender_endpoint;
        bool registered = false;

        auto start = std::chrono::steady_clock::now();
        while (!registered && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
            boost::system::error_code ec;
            size_t bytes_recvd = socket_->receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);
            if (!ec && bytes_recvd >= 10) {
                std::string response(recv_buf.data(), bytes_recvd);
                if (response.substr(0, 10) == "REGISTERED" || response.substr(0, 13) == "RE-REGISTERED") {
                    registered = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        socket_->non_blocking(false);

        if (registered) {
            qDebug() << "Successfully registered with VoIP server";
            voipStatusLabel->setText("VoIP: Connected");
            voipStatusLabel->setStyleSheet("color: green;");
            return true;
        }
        qDebug() << "VoIP registration timeout";
        voipStatusLabel->setText("VoIP: Disconnected");
        voipStatusLabel->setStyleSheet("color: red;");
        return false;
    } catch (std::exception &e) {
        qDebug() << "VoIP registration error:" << e.what();
        voipStatusLabel->setText("VoIP: Error");
        voipStatusLabel->setStyleSheet("color: red;");
        return false;
    }
}

void ChatClient::initPortAudio()
{
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw std::runtime_error("PortAudio initialization failed: " + std::string(Pa_GetErrorText(err)));
    }

    PaDeviceIndex inputDevice = Pa_GetDefaultInputDevice();
    if (inputDevice == paNoDevice) {
        throw std::runtime_error("No default input device");
    }

    PaDeviceIndex outputDevice = Pa_GetDefaultOutputDevice();
    if (outputDevice == paNoDevice) {
        throw std::runtime_error("No default output device");
    }

    PaStreamParameters inputParameters;
    inputParameters.device = inputDevice;
    inputParameters.channelCount = 1;
    inputParameters.sampleFormat = paInt16;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputDevice)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters outputParameters;
    outputParameters.device = outputDevice;
    outputParameters.channelCount = 1;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputDevice)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(
        &stream_,
        &inputParameters,
        &outputParameters,
        44100,
        1024,
        paClipOff,
        ChatClient::portAudioCallback,
        this);

    if (err != paNoError) {
        throw std::runtime_error("PortAudio stream open failed: " + std::string(Pa_GetErrorText(err)));
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        throw std::runtime_error("PortAudio stream start failed: " + std::string(Pa_GetErrorText(err)));
    }
}

void ChatClient::terminatePortAudio()
{
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    Pa_Terminate();
}

int ChatClient::portAudioCallback(const void *inputBuffer, void *outputBuffer,
                                 unsigned long framesPerBuffer,
                                 const PaStreamCallbackTimeInfo *timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void *userData)
{
    (void)timeInfo;
    (void)statusFlags;
    ChatClient *client = static_cast<ChatClient *>(userData);

    if (inputBuffer) {
        const int16_t *input = static_cast<const int16_t *>(inputBuffer);
        std::vector<int16_t> audioData(input, input + framesPerBuffer);

        std::unique_lock<std::mutex> lock(client->audio_mutex_);
        if (client->audioQueue_.size() < 100) {
            client->audioQueue_.push(std::move(audioData));
            lock.unlock();
            client->audio_cv_.notify_one();
        }
    }

    {
        std::unique_lock<std::mutex> lock(client->playback_mutex_);
        client->playback_cv_.wait_for(lock, std::chrono::milliseconds(10),
                                     [client]() { return !client->playbackQueue_.empty() || !client->running_; });

        int16_t *output = static_cast<int16_t *>(outputBuffer);

        if (!client->playbackQueue_.empty()) {
            std::vector<int16_t> &playbackData = client->playbackQueue_.front();
            size_t copySize = std::min(playbackData.size(), static_cast<size_t>(framesPerBuffer));
            std::copy(playbackData.begin(), playbackData.begin() + copySize, output);

            if (copySize < framesPerBuffer) {
                std::fill(output + copySize, output + framesPerBuffer, 0);
                client->playbackQueue_.pop();
            } else if (copySize == framesPerBuffer) {
                client->playbackQueue_.pop();
            }
        } else {
            std::fill_n(output, framesPerBuffer, 0);
        }
    }

    return client->running_ ? paContinue : paComplete;
}

void ChatClient::sendPing()
{
    socket_->async_send_to(
        boost::asio::buffer("PING", 4), endpoint_,
        boost::asio::bind_executor(strand_,
            [this](const boost::system::error_code &error, std::size_t) {
                if (error) {
                    qDebug() << "Ping error:" << error.message().c_str();
                }
            }));
}

void ChatClient::sendAudio()
{
    while (running_) {
        std::vector<int16_t> audioData;

        {
            std::unique_lock<std::mutex> lock(audio_mutex_);
            audio_cv_.wait_for(lock, std::chrono::milliseconds(100),
                               [this]() { return !audioQueue_.empty() || !running_; });

            if (!running_)
                break;

            if (!audioQueue_.empty()) {
                audioData = std::move(audioQueue_.front());
                audioQueue_.pop();
            }
        }

        if (!audioData.empty()) {
            std::string channel_header = "AUDIO " + channel_id_.toStdString() + " ";
            std::vector<char> packet;
            packet.insert(packet.end(), channel_header.begin(), channel_header.end());
            packet.insert(packet.end(),
                          reinterpret_cast<char *>(audioData.data()),
                          reinterpret_cast<char *>(audioData.data() + audioData.size()));

            socket_->async_send_to(
                boost::asio::buffer(packet), endpoint_,
                boost::asio::bind_executor(strand_,
                    [this](const boost::system::error_code &error, std::size_t) {
                        if (error) {
                            qDebug() << "Send error:" << error.message().c_str();
                        }
                    }));
        }
    }
}

void ChatClient::receive()
{
    startReceive();
    io_context_.run();
}

void ChatClient::startReceive()
{
    socket_->async_receive_from(
        boost::asio::buffer(buffer_),
        remote_endpoint_,
        boost::asio::bind_executor(strand_,
            [this](const boost::system::error_code &error, std::size_t bytes_recvd) {
                if (!error && bytes_recvd > 0) {
                    handleReceive(bytes_recvd);
                } else if (error) {
                    qDebug() << "Receive error:" << error.message().c_str();
                }
                startReceive();
            }));
}

void ChatClient::handleReceive(std::size_t bytes_recvd)
{
    std::string header(buffer_.data(), 6);
    if (header == "AUDIO ") {
        size_t channel_end = std::find(buffer_.data() + 6, buffer_.data() + bytes_recvd, ' ') - buffer_.data();
        if (channel_end >= bytes_recvd)
            return;

        size_t audio_start = channel_end + 1;
        size_t audio_size = bytes_recvd - audio_start;

        if (audio_size % sizeof(int16_t) != 0) {
            qDebug() << "Received incomplete audio sample";
            return;
        }

        std::vector<int16_t> audioData(audio_size / sizeof(int16_t));
        std::memcpy(audioData.data(), buffer_.data() + audio_start, audio_size);

        {
            std::unique_lock<std::mutex> lock(playback_mutex_);
            if (playbackQueue_.size() < 100) {
                playbackQueue_.push(std::move(audioData));
                lock.unlock();
                playback_cv_.notify_one();
            }
        }
    } else if (std::string(buffer_.data(), 4) == "PONG") {
        return;
    }
}

void ChatClient::onStartCallButtonClicked()
{
    if (currentChatId == -1 || !chatListWidget->currentItem()) {
        QMessageBox::warning(this, "Ошибка", "Выберите чат для звонка");
        return;
    }
    bool isVoiceChat = chatListWidget->currentItem()->data(Qt::UserRole + 1).toBool();
    if (!isVoiceChat) {
        QMessageBox::warning(this, "Ошибка", "Этот чат не является голосовым");
        return;
    }
    if (running_) {
        stop();
    }
    channel_id_ = QString("voice_chat_%1").arg(currentChatId);
    running_ = true;
    if (!checkConnection(channel_id_) || !registerWithServer(channel_id_)) {
        qDebug() << "Не удалось подключиться к VoIP-серверу";
        running_ = false;
        voipStatusLabel->setText("VoIP: Disconnected");
        voipStatusLabel->setStyleSheet("color: red;");
        QMessageBox::critical(this, "Ошибка", "Не удалось подключиться к VoIP-серверу");
        return;
    }
    try {
        initPortAudio();
        send_thread_ = std::thread([this]() { sendAudio(); });
        receive_thread_ = std::thread([this]() { receive(); });
        keepalive_thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                sendPing();
            }
        });
        startCallButton->setEnabled(false);
        stopCallButton->setEnabled(true);
        qDebug() << "Голосовой звонок начат для канала:" << channel_id_;
    } catch (const std::exception &e) {
        qDebug() << "Ошибка запуска VoIP:" << e.what();
        stop();
        voipStatusLabel->setText("VoIP: Error");
        voipStatusLabel->setStyleSheet("color: red;");
        QMessageBox::critical(this, "Ошибка", QString("Ошибка запуска VoIP: %1").arg(e.what()));
    }
}

void ChatClient::onStopCallButtonClicked()
{
    stop();
    startCallButton->setEnabled(true);
    stopCallButton->setEnabled(false);
    voipStatusLabel->setText("VoIP: Disconnected");
    voipStatusLabel->setStyleSheet("color: red;");
    qDebug() << "VoIP call stopped";
}

void ChatClient::stop()
{
    if (!running_)
        return;

    running_ = false;

    if (stream_) {
        Pa_StopStream(stream_);
    }

    audio_cv_.notify_all();
    playback_cv_.notify_all();

    io_context_.stop();

    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    if (keepalive_thread_.joinable()) {
        keepalive_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        std::queue<std::vector<int16_t>> empty;
        std::swap(audioQueue_, empty);
    }
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        std::queue<std::vector<int16_t>> empty;
        std::swap(playbackQueue_, empty);
    }

    terminatePortAudio();
    io_context_.restart();
}
