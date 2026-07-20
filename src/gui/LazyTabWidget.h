#pragma once
#include <QTabWidget>
#include <QHash>
#include <functional>

// QTabWidget that defers widget construction until a tab is first shown.
// Use addLazyTab() instead of addTab() for widgets you want to create on demand.
class LazyTabWidget : public QTabWidget {
public:
    explicit LazyTabWidget(QWidget *parent = nullptr) : QTabWidget(parent) {
        connect(this, &QTabWidget::currentChanged, this, [this](int index) {
            if (m_replacing || !m_factories.contains(index)) return;
            auto factory = m_factories.take(index);
            QWidget *real = factory();
            if (!real) return;

            QWidget *placeholder = widget(index);
            QString label = tabText(index);
            bool enabled = isTabEnabled(index);

            m_replacing = true;
            removeTab(index);
            insertTab(index, real, label);
            setTabEnabled(index, enabled);
            setCurrentIndex(index);
            m_replacing = false;

            if (placeholder) placeholder->deleteLater();
        });
    }

    int addLazyTab(const QString &label, std::function<QWidget*()> factory) {
        int idx = count();
        m_factories.insert(idx, std::move(factory));
        QTabWidget::addTab(new QWidget(this), label);
        return idx;
    }

private:
    QHash<int, std::function<QWidget*()>> m_factories;
    bool m_replacing = false;
};
