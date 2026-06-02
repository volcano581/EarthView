#pragma once
#ifndef MAPVIEWWINDOW_H
#define MAPVIEWWINDOW_H

#include <QMainWindow>
#include <QString>

class Camera;
class QLabel;
class MapWidget;
class QCloseEvent;
class QShowEvent;

class MapViewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MapViewWindow(const QString& title, QWidget* parent = nullptr);

    MapWidget* mapWidget() const { return m_mapWidget; }

signals:
    void windowVisibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupToolbar();
    void createStatusBar();
    void updateStatus();

private:
    MapWidget* m_mapWidget;
    QLabel* m_coordLabel;
    QLabel* m_zoomLabel;
};

#endif // MAPVIEWWINDOW_H
