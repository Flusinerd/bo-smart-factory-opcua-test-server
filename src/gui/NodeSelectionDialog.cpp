#include "NodeSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QApplication>

NodeSelectionDialog::NodeSelectionDialog(const QString &endpointUrl,
                                         const QString &namespaceUri,
                                         const QVector<DiscoveredNode> &cachedNodes,
                                         QWidget *parent)
    : QDialog(parent)
    , m_endpointUrl(endpointUrl)
    , m_namespaceUri(namespaceUri)
    , m_nodes(cachedNodes)
    , m_browser(new OpcUaBrowser(this))
{
    setWindowTitle(tr("Select OPC UA Node"));
    setMinimumWidth(400);

    auto *layout = new QVBoxLayout(this);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter nodes..."));
    connect(m_filterEdit, &QLineEdit::textChanged, this, &NodeSelectionDialog::onFilterChanged);
    layout->addWidget(m_filterEdit);

    m_combo = new QComboBox(this);
    m_combo->setEditable(false);
    m_combo->setMinimumContentsLength(30);
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NodeSelectionDialog::onSelectionChanged);
    layout->addWidget(m_combo);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *btnLayout = new QHBoxLayout();
    m_discoverButton = new QPushButton(tr("Discover Nodes"), this);
    connect(m_discoverButton, &QPushButton::clicked, this, &NodeSelectionDialog::onDiscover);
    btnLayout->addWidget(m_discoverButton);

    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = bbox->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    btnLayout->addWidget(bbox);

    layout->addLayout(btnLayout);

    if (!m_nodes.isEmpty()) {
        populateCombo();
        m_statusLabel->setText(tr("Showing %1 cached variable(s). Click \"Discover Nodes\" to refresh.").arg(m_nodes.size()));
    } else {
        m_statusLabel->setText(tr("Click \"Discover Nodes\" to browse the OPC UA server."));
    }
}

void NodeSelectionDialog::onDiscover()
{
    if (m_endpointUrl.isEmpty() || m_namespaceUri.isEmpty()) {
        m_statusLabel->setText(tr("Please set endpoint URL and namespace URI first."));
        return;
    }
    setBusy(true);
    m_statusLabel->setText(tr("Discovering nodes..."));
    QApplication::processEvents();

    bool success = m_browser->connectAndBrowse(m_endpointUrl, m_namespaceUri);
    if (success) {
        m_nodes = m_browser->discoveredNodes();
    }
    m_browser->disconnect();

    setBusy(false);
    if (success) {
        populateCombo();
        m_statusLabel->setText(tr("Found %1 variable(s).").arg(m_nodes.size()));
    } else {
        m_statusLabel->setText(tr("Discovery failed."));
    }
}

void NodeSelectionDialog::onDiscoveryFinished(bool success, const QString &error)
{
    (void)success;
    (void)error;
}

void NodeSelectionDialog::onFilterChanged(const QString &text)
{
    m_filteredNodes.clear();
    QString lower = text.toLower();
    for (const auto &n : m_nodes) {
        if (lower.isEmpty() ||
            n.browseName.toLower().contains(lower) ||
            n.displayName.toLower().contains(lower) ||
            n.nodeIdString.toLower().contains(lower)) {
            m_filteredNodes.append(n);
        }
    }
    populateCombo();
}

void NodeSelectionDialog::onSelectionChanged()
{
    m_okButton->setEnabled(m_combo->currentIndex() >= 0 && m_combo->count() > 0);
}

void NodeSelectionDialog::populateCombo()
{
    m_combo->clear();
    const QVector<DiscoveredNode> &src = m_filteredNodes.isEmpty() ? m_nodes : m_filteredNodes;
    for (const auto &n : src) {
        m_combo->addItem(QStringLiteral("%1 (%2)").arg(n.browseName, n.nodeIdString),
                         n.nodeIdString);
    }
    onSelectionChanged();
}

void NodeSelectionDialog::setBusy(bool busy)
{
    m_discoverButton->setEnabled(!busy);
    m_combo->setEnabled(!busy);
}

QString NodeSelectionDialog::selectedNodeIdString() const
{
    int idx = m_combo->currentIndex();
    if (idx < 0)
        return QString();
    return m_combo->itemData(idx).toString();
}

QString NodeSelectionDialog::selectedBrowseName() const
{
    if (m_combo->currentIndex() < 0 || m_combo->count() == 0)
        return QString();
    QString text = m_combo->currentText();
    int pos = text.indexOf(" (");
    return pos >= 0 ? text.left(pos) : text;
}

quint8 NodeSelectionDialog::selectedTypeCode() const
{
    return 0;
}
