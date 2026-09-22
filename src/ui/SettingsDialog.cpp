#include "SettingsDialog.h"

#include "core/HistoryStore.h"
#include "ui/Theme.h"
#include "ui/ToggleSwitch.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLabel *secondaryLabel(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("secondaryText"));
    label->setWordWrap(true);
    return label;
}

QWidget *settingsCard(QWidget *parent = nullptr) {
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    return card;
}

}

SettingsDialog::SettingsDialog(const AppSettings &settings, HistoryStore *store,
                               QWidget *parent)
    : QDialog(parent), original_(settings), store_(store) {
    setWindowTitle(QStringLiteral("设置"));
    resize(760, 500);
    setMinimumSize(680, 450);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 16);
    root->setSpacing(12);

    auto *body = new QHBoxLayout;
    body->setSpacing(18);

    nav_ = new QListWidget(this);
    nav_->setObjectName(QStringLiteral("settingsNav"));
    nav_->setFixedWidth(142);
    nav_->setSpacing(2);
    nav_->addItems({
        QStringLiteral("常规"),
        QStringLiteral("粘贴"),
        QStringLiteral("历史记录"),
        QStringLiteral("外观"),
        QStringLiteral("关于")
    });

    pages_ = new QStackedWidget(this);
    pages_->addWidget(makeGeneralPage());
    pages_->addWidget(makePastePage());
    pages_->addWidget(makeHistoryPage());
    pages_->addWidget(makeAppearancePage());
    pages_->addWidget(makeAboutPage());

    body->addWidget(nav_);
    body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();

    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("secondaryButton"));
    auto *save = new QPushButton(QStringLiteral("保存"), this);
    save->setProperty("primary", true);
    save->setDefault(true);

    buttons->addWidget(cancel);
    buttons->addWidget(save);
    root->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    connect(nav_, &QListWidget::currentRowChanged, this,
            [this](int row) { switchPage(row); });

    nav_->setCurrentRow(0);
    updateHistoryHint();
}

QWidget *SettingsDialog::makePage(const QString &title, const QString &subtitle,
                                  QWidget *content) {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 2, 2, 2);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("settingsTitle"));
    auto *subtitleLabel = secondaryLabel(subtitle, page);

    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    layout->addSpacing(6);
    layout->addWidget(content);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::makeToggleRow(const QString &title,
                                       const QString &description,
                                       ToggleSwitch *toggle,
                                       QWidget *parent) {
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("settingRowTitle"));
    auto *descriptionLabel = secondaryLabel(description, row);

    texts->addWidget(titleLabel);
    texts->addWidget(descriptionLabel);

    layout->addLayout(texts, 1);
    layout->addWidget(toggle, 0, Qt::AlignVCenter);
    return row;
}

QWidget *SettingsDialog::makeControlRow(const QString &title,
                                        const QString &description,
                                        QWidget *control,
                                        QWidget *parent) {
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *titleLabel = new QLabel(title, row);
    titleLabel->setObjectName(QStringLiteral("settingRowTitle"));
    auto *descriptionLabel = secondaryLabel(description, row);

    texts->addWidget(titleLabel);
    texts->addWidget(descriptionLabel);

    layout->addLayout(texts, 1);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

QWidget *SettingsDialog::makeGeneralPage() {
    auto *card = settingsCard(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    startMinimized_ = new ToggleSwitch(card);
    startMinimized_->setChecked(original_.startMinimized);
    closeToTray_ = new ToggleSwitch(card);
    closeToTray_->setChecked(original_.closeToTray);

    layout->addWidget(makeToggleRow(
        QStringLiteral("启动时仅驻留托盘"),
        QStringLiteral("启动 Cliprove 后不自动弹出主窗口。"),
        startMinimized_, card));
    layout->addWidget(makeToggleRow(
        QStringLiteral("关闭窗口后继续运行"),
        QStringLiteral("关闭主窗口时保持后台监听剪贴板。"),
        closeToTray_, card));

    return makePage(
        QStringLiteral("常规"),
        QStringLiteral("控制 Cliprove 的后台运行行为。"), card);
}

QWidget *SettingsDialog::makePastePage() {
    auto *card = settingsCard(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    clickToPaste_ = new ToggleSwitch(card);
    clickToPaste_->setChecked(original_.clickToPaste);

    batchInterval_ = new QSpinBox(card);
    batchInterval_->setRange(200, 5000);
    batchInterval_->setSingleStep(100);
    batchInterval_->setSuffix(QStringLiteral(" ms"));
    batchInterval_->setValue(original_.batchIntervalMs);

    layout->addWidget(makeToggleRow(
        QStringLiteral("单击即粘贴"),
        QStringLiteral("单击一条历史记录时直接粘贴到之前的窗口。"),
        clickToPaste_, card));
    layout->addWidget(makeControlRow(
        QStringLiteral("多项粘贴间隔"),
        QStringLiteral("目标程序较慢时可适当增加这个间隔。"),
        batchInterval_, card));

    return makePage(
        QStringLiteral("粘贴"),
        QStringLiteral("控制历史记录的粘贴方式与批量节奏。"), card);
}

QWidget *SettingsDialog::makeHistoryPage() {
    auto *card = settingsCard(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    historyLimit_ = new QSpinBox(card);
    historyLimit_->setRange(0, 100000);
    historyLimit_->setSpecialValueText(QStringLiteral("无限制"));
    historyLimit_->setValue(original_.maxUnstarredEntries);

    historyHint_ = secondaryLabel(QString(), card);
    historyHint_->setContentsMargins(14, 6, 14, 10);

    layout->addWidget(makeControlRow(
        QStringLiteral("保留普通历史"),
        QStringLiteral("收藏记录不受数量限制影响。"),
        historyLimit_, card));
    layout->addWidget(historyHint_);

    connect(historyLimit_, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] { updateHistoryHint(); });

    return makePage(
        QStringLiteral("历史记录"),
        QStringLiteral("控制本地历史记录的保留数量。"), card);
}

QWidget *SettingsDialog::makeAppearancePage() {
    auto *card = settingsCard(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    showThumbnails_ = new ToggleSwitch(card);
    showThumbnails_->setChecked(original_.showThumbnails);

    thumbnailSize_ = new QSpinBox(card);
    thumbnailSize_->setRange(40, 128);
    thumbnailSize_->setSuffix(QStringLiteral(" px"));
    thumbnailSize_->setValue(original_.thumbnailSize);
    thumbnailSize_->setEnabled(original_.showThumbnails);

    compactRows_ = new ToggleSwitch(card);
    compactRows_->setChecked(original_.compactRows);

    animationsEnabled_ = new ToggleSwitch(card);
    animationsEnabled_->setChecked(original_.animationsEnabled);

    connect(showThumbnails_, &ToggleSwitch::toggled,
            thumbnailSize_, &QWidget::setEnabled);

    layout->addWidget(makeToggleRow(
        QStringLiteral("显示图片缩略图"),
        QStringLiteral("在图片和本地图片文件记录旁显示预览。"),
        showThumbnails_, card));
    layout->addWidget(makeControlRow(
        QStringLiteral("缩略图大小"),
        QStringLiteral("只影响历史列表中的图片预览大小。"),
        thumbnailSize_, card));
    layout->addWidget(makeToggleRow(
        QStringLiteral("紧凑列表"),
        QStringLiteral("减少历史记录行距，在同一窗口显示更多内容。"),
        compactRows_, card));
    layout->addWidget(makeToggleRow(
        QStringLiteral("界面动画"),
        QStringLiteral("启用窗口和设置页面的轻量过渡动画。"),
        animationsEnabled_, card));

    return makePage(
        QStringLiteral("外观"),
        QStringLiteral("保持简洁，并跟随系统浅色或深色配色。"), card);
}

QWidget *SettingsDialog::makeAboutPage() {
    auto *card = settingsCard(this);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(18);

    auto *icon = new QLabel(card);
    icon->setPixmap(Theme::appIcon().pixmap(QSize(72, 72)));
    icon->setFixedSize(76, 76);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(4);
    auto *name = new QLabel(QStringLiteral("Cliprove"), card);
    name->setObjectName(QStringLiteral("aboutName"));
    auto *version = secondaryLabel(QStringLiteral("Preview 0.1.0"), card);
    auto *description = secondaryLabel(
        QStringLiteral("一个面向 Linux 的轻量剪贴板历史工具。\n"
                       "多 MIME 历史、本地保存、直接粘贴。"), card);
    auto *repo = secondaryLabel(QStringLiteral("github.com/veexi/Cliprove"), card);

    texts->addWidget(name);
    texts->addWidget(version);
    texts->addSpacing(4);
    texts->addWidget(description);
    texts->addSpacing(4);
    texts->addWidget(repo);

    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addLayout(texts, 1);

    return makePage(
        QStringLiteral("关于"),
        QStringLiteral("项目与版本信息。"), card);
}

void SettingsDialog::switchPage(int index, bool animate) {
    if (index < 0 || index >= pages_->count())
        return;

    pages_->setCurrentIndex(index);
    if (!animate || !original_.animationsEnabled)
        return;

    auto *page = pages_->currentWidget();
    auto *effect = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(effect);
    effect->setOpacity(0.25);

    auto *animation = new QPropertyAnimation(effect, "opacity", page);
    animation->setDuration(140);
    animation->setStartValue(0.25);
    animation->setEndValue(1.0);
    connect(animation, &QPropertyAnimation::finished, page, [page] {
        page->setGraphicsEffect(nullptr);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void SettingsDialog::updateHistoryHint() {
    if (!historyHint_ || !historyLimit_)
        return;

    if (historyLimit_->value() == 0) {
        historyHint_->setText(
            QStringLiteral("当前不按条数自动清理普通历史。"));
    } else {
        historyHint_->setText(
            QStringLiteral("超过 %1 条后会优先清理最旧的普通记录。")
                .arg(historyLimit_->value()));
    }
}

AppSettings SettingsDialog::settings() const {
    AppSettings result = original_;
    result.batchIntervalMs = batchInterval_->value();
    result.maxUnstarredEntries = historyLimit_->value();
    result.thumbnailSize = thumbnailSize_->value();
    result.showThumbnails = showThumbnails_->isChecked();
    result.clickToPaste = clickToPaste_->isChecked();
    result.animationsEnabled = animationsEnabled_->isChecked();
    result.compactRows = compactRows_->isChecked();
    result.startMinimized = startMinimized_->isChecked();
    result.closeToTray = closeToTray_->isChecked();
    return result;
}
