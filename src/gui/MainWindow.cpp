#include "MainWindow.h"
#include "BridgeEngine.h"
#include "NodeSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QSet>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_engine(new BridgeEngine(this))
{
    setupUi();
    setupMenu();
    autoLoadConfig();
    connect(m_engine, &BridgeEngine::statusChanged, this, &MainWindow::onStatusChanged);
    connect(m_engine, &BridgeEngine::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_engine, &BridgeEngine::clientCountChanged, this, &MainWindow::onClientCountChanged);
    updateUiState();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    auto *connGroup = new QGroupBox(tr("Connection"), this);
    auto *connLayout = new QFormLayout(connGroup);
    m_endpointEdit = new QLineEdit(this);
    m_endpointEdit->setPlaceholderText("opc.tcp://localhost:4840");
    m_endpointEdit->setText("opc.tcp://localhost:4840");
    connLayout->addRow(tr("Endpoint URL:"), m_endpointEdit);

    m_namespaceEdit = new QLineEdit(this);
    m_namespaceEdit->setPlaceholderText("urn:binary-sensors-demo");
    m_namespaceEdit->setText("urn:binary-sensors-demo");
    connLayout->addRow(tr("Namespace URI:"), m_namespaceEdit);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(9000);
    connLayout->addRow(tr("TCP port:"), m_portSpin);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_disconnectButton = new QPushButton(tr("Disconnect"), this);
    m_startButton = new QPushButton(tr("Start Bridge"), this);
    m_stopButton = new QPushButton(tr("Stop Bridge"), this);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartBridge);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStopBridge);
    btnRow->addWidget(m_connectButton);
    btnRow->addWidget(m_disconnectButton);
    btnRow->addWidget(m_startButton);
    btnRow->addWidget(m_stopButton);
    connLayout->addRow(btnRow);
    layout->addWidget(connGroup);

    auto *mapGroup = new QGroupBox(tr("Sensor Mappings"), this);
    auto *mapLayout = new QVBoxLayout(mapGroup);
    m_addSensorButton = new QPushButton(tr("Add Sensor"), this);
    connect(m_addSensorButton, &QPushButton::clicked, this, &MainWindow::onAddSensor);
    mapLayout->addWidget(m_addSensorButton);

    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setColumnCount(5);
    m_mappingTable->setHorizontalHeaderLabels(
        {tr("Enabled"), tr("QR Code ID"), tr("OPC UA Node"), tr("Type"), tr("Actions")});
    m_mappingTable->horizontalHeader()->setStretchLastSection(true);
    m_mappingTable->verticalHeader()->setDefaultSectionSize(32);
    m_mappingTable->setSelectionBehavior(QTableWidget::SelectRows);
    connect(m_mappingTable, &QTableWidget::cellChanged, this, &MainWindow::onMappingCellChanged);
    mapLayout->addWidget(m_mappingTable);

    layout->addWidget(mapGroup);

    auto *statusBar = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_clientLabel = new QLabel(this);
    statusBar->addWidget(m_statusLabel);
    statusBar->addWidget(m_clientLabel);
    statusBar->addStretch();
    layout->addLayout(statusBar);
}

void MainWindow::setupMenu()
{
    m_fileMenu = menuBar()->addMenu(tr("File"));
    auto *saveAct = m_fileMenu->addAction(tr("Save Configuration..."));
    auto *loadAct = m_fileMenu->addAction(tr("Load Configuration..."));
    connect(saveAct, &QAction::triggered, this, &MainWindow::onSaveConfig);
    connect(loadAct, &QAction::triggered, this, &MainWindow::onLoadConfig);
}

void MainWindow::onConnect()
{
    m_engine->setEndpointUrl(m_endpointEdit->text().trimmed());
    m_engine->setNamespaceUri(m_namespaceEdit->text().trimmed());
    m_engine->setTcpPort(static_cast<quint16>(m_portSpin->value()));
    m_engine->setMappings(mappingsFromTable());
    m_engine->connectToOpcUa();
    updateUiState();
}

void MainWindow::onDisconnect()
{
    m_engine->stopBridge();
    m_engine->disconnectFromOpcUa();
    updateUiState();
}

void MainWindow::onStartBridge()
{
    loadMappingsFromTable();
    m_engine->setEndpointUrl(m_endpointEdit->text().trimmed());
    m_engine->setNamespaceUri(m_namespaceEdit->text().trimmed());
    m_engine->setTcpPort(static_cast<quint16>(m_portSpin->value()));
    m_engine->setMappings(mappingsFromTable());
    if (!m_engine->isOpcUaConnected())
        m_engine->connectToOpcUa();
    if (m_engine->isOpcUaConnected())
        m_engine->startBridge();
    updateUiState();
}

void MainWindow::onStopBridge()
{
    m_engine->stopBridge();
    updateUiState();
}

void MainWindow::onAddSensor()
{
    const int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);
    m_mappingTable->setCellWidget(row, 0, nullptr);

    auto *enabledCheck = new QCheckBox(this);
    enabledCheck->setChecked(true);
    m_mappingTable->setCellWidget(row, 0, enabledCheck);

    m_mappingTable->setItem(row, 1, new QTableWidgetItem());
    m_mappingTable->setItem(row, 2, new QTableWidgetItem());
    m_mappingTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("0")));

    auto *selectBtn = new QPushButton(tr("Select node..."), this);
    auto *deleteBtn = new QPushButton(tr("Delete"), this);
    const int rowButtonHeight = 28;
    selectBtn->setMinimumHeight(rowButtonHeight);
    deleteBtn->setMinimumHeight(rowButtonHeight);
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(8);
    btnLayout->addWidget(selectBtn);
    btnLayout->addWidget(deleteBtn);
    auto *btnWidget = new QWidget(this);
    btnWidget->setLayout(btnLayout);
    m_mappingTable->setCellWidget(row, 4, btnWidget);

    connect(selectBtn, &QPushButton::clicked, this, [this, selectBtn]() {
        for (int r = 0; r < m_mappingTable->rowCount(); ++r) {
            if (m_mappingTable->cellWidget(r, 4) == selectBtn->parentWidget()) {
                onSelectNode(r);
                return;
            }
        }
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this, deleteBtn]() {
        for (int r = 0; r < m_mappingTable->rowCount(); ++r) {
            if (m_mappingTable->cellWidget(r, 4) == deleteBtn->parentWidget()) {
                m_mappingTable->removeRow(r);
                return;
            }
        }
    });
}

void MainWindow::onDeleteSensor()
{
    int row = m_mappingTable->currentRow();
    if (row >= 0)
        m_mappingTable->removeRow(row);
}

void MainWindow::onSelectNode(int row)
{
    QString endpoint = m_endpointEdit->text().trimmed();
    QString namespaceUri = m_namespaceEdit->text().trimmed();
    QVector<DiscoveredNode> cached = (endpoint == m_cachedDiscoveryEndpoint && namespaceUri == m_cachedDiscoveryNamespace)
        ? m_cachedDiscoveredNodes : QVector<DiscoveredNode>();
    NodeSelectionDialog dlg(endpoint, namespaceUri, cached, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_cachedDiscoveredNodes = dlg.discoveredNodes();
        m_cachedDiscoveryEndpoint = endpoint;
        m_cachedDiscoveryNamespace = namespaceUri;

        QString nodeId = dlg.selectedNodeIdString();
        QString browseName = dlg.selectedBrowseName();
        if (!nodeId.isEmpty()) {
            QTableWidgetItem *nodeItem = m_mappingTable->item(row, 2);
            if (!nodeItem)
                nodeItem = new QTableWidgetItem();
            nodeItem->setText(nodeId);
            m_mappingTable->setItem(row, 2, nodeItem);

            QTableWidgetItem *idItem = m_mappingTable->item(row, 1);
            if (idItem && idItem->text().isEmpty())
                idItem->setText(browseName);
            else if (!idItem) {
                idItem = new QTableWidgetItem(browseName);
                m_mappingTable->setItem(row, 1, idItem);
            }
        }
    }
}

void MainWindow::onStatusChanged(const QString &status)
{
    m_statusLabel->setText(status);
}

void MainWindow::onErrorOccurred(const QString &error)
{
    QMessageBox::warning(this, tr("Error"), error);
}

void MainWindow::onClientCountChanged(int count)
{
    m_clientLabel->setText(tr("TCP clients: %1").arg(count));
}

void MainWindow::onSaveConfig()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Configuration"),
                                               QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty())
        return;

    QJsonObject root;
    root["endpointUrl"] = m_endpointEdit->text().trimmed();
    root["namespaceUri"] = m_namespaceEdit->text().trimmed();
    root["tcpPort"] = m_portSpin->value();

    QJsonArray mappingsArr;
    for (const auto &m : mappingsFromTable()) {
        mappingsArr.append(m.toJson());
    }
    root["mappings"] = mappingsArr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file for writing."));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::onLoadConfig()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Load Configuration"),
                                                QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file."));
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid JSON: %1").arg(err.errorString()));
        return;
    }

    QJsonObject root = doc.object();
    m_endpointEdit->setText(root["endpointUrl"].toString());
    m_namespaceEdit->setText(root["namespaceUri"].toString());
    m_portSpin->setValue(root["tcpPort"].toInt(9000));

    QVector<SensorMapping> mappings;
    for (const QJsonValue &v : root["mappings"].toArray()) {
        mappings.append(SensorMapping::fromJson(v.toObject()));
    }
    showMappingsInTable(mappings);
}

void MainWindow::onMappingCellChanged(int row, int column)
{
    (void)row;
    (void)column;
}

void MainWindow::updateUiState()
{
    bool connected = m_engine->isOpcUaConnected();
    bool running = m_engine->isBridgeRunning();

    m_connectButton->setEnabled(!connected && !running);
    m_disconnectButton->setEnabled(connected || running);
    m_startButton->setEnabled(connected && !running);
    m_stopButton->setEnabled(running);
    m_addSensorButton->setEnabled(true);

    m_endpointEdit->setEnabled(!running);
    m_namespaceEdit->setEnabled(!running);
    m_portSpin->setEnabled(!running);
}

void MainWindow::loadMappingsFromTable()
{
}

QVector<SensorMapping> MainWindow::mappingsFromTable() const
{
    QVector<SensorMapping> out;
    for (int r = 0; r < m_mappingTable->rowCount(); ++r) {
        SensorMapping m;
        QWidget *w = m_mappingTable->cellWidget(r, 0);
        auto *cb = qobject_cast<QCheckBox *>(w);
        m.enabled = cb ? cb->isChecked() : true;

        QTableWidgetItem *idItem = m_mappingTable->item(r, 1);
        m.messagePackId = idItem ? idItem->text().trimmed() : QString();

        QTableWidgetItem *nodeItem = m_mappingTable->item(r, 2);
        m.nodeIdString = nodeItem ? nodeItem->text().trimmed() : QString();

        QTableWidgetItem *typeItem = m_mappingTable->item(r, 3);
        m.typeCode = static_cast<quint8>(typeItem ? typeItem->text().toInt() : 0);

        out.append(m);
    }
    return out;
}

void MainWindow::showMappingsInTable(const QVector<SensorMapping> &mappings)
{
    m_mappingTable->setRowCount(0);
    for (const auto &m : mappings) {
        const int row = m_mappingTable->rowCount();
        m_mappingTable->insertRow(row);

        auto *enabledCheck = new QCheckBox(this);
        enabledCheck->setChecked(m.enabled);
        m_mappingTable->setCellWidget(row, 0, enabledCheck);

        m_mappingTable->setItem(row, 1, new QTableWidgetItem(m.messagePackId));
        m_mappingTable->setItem(row, 2, new QTableWidgetItem(m.nodeIdString));
        m_mappingTable->setItem(row, 3, new QTableWidgetItem(QString::number(m.typeCode)));

        auto *selectBtn = new QPushButton(tr("Select node..."), this);
        auto *deleteBtn = new QPushButton(tr("Delete"), this);
        const int rowButtonHeight = 28;
        selectBtn->setMinimumHeight(rowButtonHeight);
        deleteBtn->setMinimumHeight(rowButtonHeight);
        auto *btnLayout = new QHBoxLayout();
        btnLayout->setContentsMargins(0, 0, 0, 0);
        btnLayout->setSpacing(8);
        btnLayout->addWidget(selectBtn);
        btnLayout->addWidget(deleteBtn);
        auto *btnWidget = new QWidget(this);
        btnWidget->setLayout(btnLayout);
        m_mappingTable->setCellWidget(row, 4, btnWidget);

        connect(selectBtn, &QPushButton::clicked, this, [this, selectBtn]() {
            for (int r = 0; r < m_mappingTable->rowCount(); ++r) {
                if (m_mappingTable->cellWidget(r, 4) == selectBtn->parentWidget()) {
                    onSelectNode(r);
                    return;
                }
            }
        });
        connect(deleteBtn, &QPushButton::clicked, this, [this, deleteBtn]() {
            for (int r = 0; r < m_mappingTable->rowCount(); ++r) {
                if (m_mappingTable->cellWidget(r, 4) == deleteBtn->parentWidget()) {
                    m_mappingTable->removeRow(r);
                    return;
                }
            }
        });
    }
}

bool MainWindow::validateMappings(QString *error) const
{
    QSet<QString> ids;
    for (const auto &m : mappingsFromTable()) {
        if (!m.nodeIdString.isEmpty() && m.enabled) {
            QString id = m.messagePackId.isEmpty() ? m.nodeIdString : m.messagePackId;
            if (ids.contains(id)) {
                if (error)
                    *error = tr("Duplicate QR Code ID: %1").arg(id);
                return false;
            }
            ids.insert(id);
        }
    }
    return true;
}

QString MainWindow::autoSaveFilePath() const
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty())
        return QString();

    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    return dir.filePath(QStringLiteral("autosave.json"));
}

void MainWindow::autoSaveConfig() const
{
    const QString path = autoSaveFilePath();
    if (path.isEmpty())
        return;

    QJsonObject root;
    root["endpointUrl"] = m_endpointEdit->text().trimmed();
    root["namespaceUri"] = m_namespaceEdit->text().trimmed();
    root["tcpPort"] = m_portSpin->value();

    QJsonArray mappingsArr;
    for (const auto &m : mappingsFromTable()) {
        mappingsArr.append(m.toJson());
    }
    root["mappings"] = mappingsArr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void MainWindow::autoLoadConfig()
{
    const QString path = autoSaveFilePath();
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        return;
    }

    QJsonObject root = doc.object();
    m_endpointEdit->setText(root["endpointUrl"].toString(m_endpointEdit->text()));
    m_namespaceEdit->setText(root["namespaceUri"].toString(m_namespaceEdit->text()));
    m_portSpin->setValue(root["tcpPort"].toInt(m_portSpin->value()));

    QVector<SensorMapping> mappings;
    const QJsonArray mappingsArray = root["mappings"].toArray();
    for (const QJsonValue &v : mappingsArray) {
        mappings.append(SensorMapping::fromJson(v.toObject()));
    }
    if (!mappings.isEmpty()) {
        showMappingsInTable(mappings);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    autoSaveConfig();
    QMainWindow::closeEvent(event);
}
