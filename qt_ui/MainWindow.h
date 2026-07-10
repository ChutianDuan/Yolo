#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QMainWindow>
#include <QString>

#include <opencv2/core.hpp>

#include "config/app_config.h"
#include "model/inference_types.h"
#include "model/yolo_engine.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QResizeEvent;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void chooseConfig();
    void chooseImage();
    void loadModel();
    void runInference();
    bool ensureModelLoaded();
    bool loadModelFromUi(bool show_success);

    QString defaultConfigPath() const;
    QString resolvedPath(const QString& path) const;
    QString detectionLabel(const yolo::Detection& detection) const;

    cv::Mat drawDetections(
        const cv::Mat& image,
        const std::vector<yolo::Detection>& detections
    ) const;
    void renderImage(
        const cv::Mat& image,
        const std::vector<yolo::Detection>& detections = {}
    );
    void setImagePixmap();
    void updateResultText(const yolo::InferResult& result) const;

    static QImage matToQImage(const cv::Mat& image);

    QLineEdit* configPathEdit_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* imageInfoLabel_ = nullptr;
    QTextEdit* resultText_ = nullptr;
    QPushButton* runButton_ = nullptr;

    yolo::AppConfig config_;
    std::unique_ptr<yolo::YoloEngine> engine_;
    QString loadedConfigPath_;
    QString currentImagePath_;
    cv::Mat currentImage_;
    QImage renderedImage_;
};
