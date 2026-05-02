#ifndef NODESELECTIONDIALOG_H
#define NODESELECTIONDIALOG_H

#include <QDialog>
#include <QVector>
#include "OpcUaBrowser.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;

class NodeSelectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NodeSelectionDialog(const QString &endpointUrl,
                                const QString &namespaceUri,
                                const QVector<DiscoveredNode> &cachedNodes = {},
                                QWidget *parent = nullptr);

    QString selectedNodeIdString() const;
    QString selectedBrowseName() const;
    quint8 selectedTypeCode() const;
    QVector<DiscoveredNode> discoveredNodes() const { return m_nodes; }

private slots:
    void onDiscover();
    void onDiscoveryFinished(bool success, const QString &error);
    void onFilterChanged(const QString &text);
    void onSelectionChanged();

private:
    void populateCombo();
    void setBusy(bool busy);

    QString m_endpointUrl;
    QString m_namespaceUri;
    QVector<DiscoveredNode> m_nodes;
    QVector<DiscoveredNode> m_filteredNodes;

    QLineEdit *m_filterEdit = nullptr;
    QComboBox *m_combo = nullptr;
    QPushButton *m_discoverButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    OpcUaBrowser *m_browser = nullptr;
};

#endif
