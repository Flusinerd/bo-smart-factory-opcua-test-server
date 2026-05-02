#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include "SensorMapping.h"
#include "OpcUaBrowser.h"

class BridgeEngine;
class QTableWidget;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QMenu;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnect();
    void onDisconnect();
    void onStartBridge();
    void onStopBridge();
    void onAddSensor();
    void onDeleteSensor();
    void onSelectNode(int row);
    void onStatusChanged(const QString &status);
    void onErrorOccurred(const QString &error);
    void onClientCountChanged(int count);
    void onSaveConfig();
    void onLoadConfig();
    void onMappingCellChanged(int row, int column);

private:
    void setupUi();
    void setupMenu();
    void updateUiState();
    void loadMappingsFromTable();
    QVector<SensorMapping> mappingsFromTable() const;
    void showMappingsInTable(const QVector<SensorMapping> &mappings);
    bool validateMappings(QString *error) const;

    void autoSaveConfig() const;
    void autoLoadConfig();
    QString autoSaveFilePath() const;

    void closeEvent(QCloseEvent *event) override;

    BridgeEngine *m_engine = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLineEdit *m_namespaceEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_addSensorButton = nullptr;
    QTableWidget *m_mappingTable = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_clientLabel = nullptr;

    QMenu *m_fileMenu = nullptr;

    QVector<DiscoveredNode> m_cachedDiscoveredNodes;
    QString m_cachedDiscoveryEndpoint;
    QString m_cachedDiscoveryNamespace;
};

#endif
