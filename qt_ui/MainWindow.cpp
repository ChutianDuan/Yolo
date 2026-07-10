#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "image/image_processing.h"

namespace {

class WaitCursor {
public:
    WaitCursor() {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    }

    ~WaitCursor() {
        QApplication::restoreOverrideCursor();
    }
};

int boxLineWidth(const cv::Mat& image) {
    const int base = std::min(image.cols, image.rows);
    return std::max(2, static_cast<int>(std::round(static_cast<double>(base) / 360.0)));
}

cv::Scalar classColor(int class_id) {
    static const cv::Scalar colors[] = {
        cv::Scalar(40, 160, 240),
        cv::Scalar(80, 200, 120),
        cv::Scalar(220, 120, 80),
        cv::Scalar(180, 120, 220),
        cv::Scalar(80, 180, 220),
        cv::Scalar(220, 180, 70),
    };
    constexpr int color_count = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
    const int index = class_id >= 0 ? class_id % color_count : 0;
    return colors[index];
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildUi();
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* main_layout = new QVBoxLayout(central);

    auto* config_layout = new QHBoxLayout();
    configPathEdit_ = new QLineEdit(defaultConfigPath(), this);
    configPathEdit_->setMinimumWidth(360);

    auto* choose_config_button = new QPushButton("Config...", this);
    auto* load_model_button = new QPushButton("Load Model", this);
    config_layout->addWidget(new QLabel("Config:", this));
    config_layout->addWidget(configPathEdit_, 1);
    config_layout->addWidget(choose_config_button);
    config_layout->addWidget(load_model_button);

    auto* action_layout = new QHBoxLayout();
    auto* choose_image_button = new QPushButton("Open Image", this);
    runButton_ = new QPushButton("Run", this);
    runButton_->setEnabled(false);
    imageInfoLabel_ = new QLabel("No image selected", this);
    imageInfoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    action_layout->addWidget(choose_image_button);
    action_layout->addWidget(runButton_);
    action_layout->addWidget(imageInfoLabel_, 1);

    imageLabel_ = new QLabel("Open an image to start", this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(640, 360);
    imageLabel_->setFrameShape(QFrame::StyledPanel);
    imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    resultText_ = new QTextEdit(this);
    resultText_->setReadOnly(true);
    resultText_->setMinimumWidth(300);
    resultText_->setPlainText("Detection results will appear here.");

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(imageLabel_);
    splitter->addWidget(resultText_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    main_layout->addLayout(config_layout);
    main_layout->addLayout(action_layout);
    main_layout->addWidget(splitter, 1);

    setCentralWidget(central);
    resize(1100, 720);
    setWindowTitle("YOLO Qt Viewer");
    statusBar()->showMessage("Ready");

    connect(choose_config_button, &QPushButton::clicked, this, [this]() {
        chooseConfig();
    });
    connect(load_model_button, &QPushButton::clicked, this, [this]() {
        loadModel();
    });
    connect(choose_image_button, &QPushButton::clicked, this, [this]() {
        chooseImage();
    });
    connect(runButton_, &QPushButton::clicked, this, [this]() {
        runInference();
    });
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    setImagePixmap();
}

void MainWindow::chooseConfig() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select config",
        QFileInfo(resolvedPath(configPathEdit_->text())).absolutePath(),
        "YAML files (*.yaml *.yml);;All files (*)"
    );
    if (path.isEmpty()) {
        return;
    }

    const QString absolute_path = resolvedPath(path);
    configPathEdit_->setText(absolute_path);
    if (absolute_path != loadedConfigPath_) {
        engine_.reset();
        loadedConfigPath_.clear();
        statusBar()->showMessage("Config changed. Load model before running inference.");
    }
}

void MainWindow::chooseImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open image",
        QDir::currentPath(),
        "Images (*.jpg *.jpeg *.png *.bmp *.webp);;All files (*)"
    );
    if (path.isEmpty()) {
        return;
    }

    cv::Mat image = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        QMessageBox::warning(this, "Open image", "Failed to read the selected image.");
        return;
    }

    currentImage_ = image;
    currentImagePath_ = path;
    runButton_->setEnabled(true);
    imageInfoLabel_->setText(
        QString("%1  %2 x %3").arg(QFileInfo(path).fileName()).arg(image.cols).arg(image.rows)
    );
    resultText_->setPlainText("Image loaded. Press Run to infer.");
    renderImage(currentImage_);
    statusBar()->showMessage("Image loaded");
}

void MainWindow::loadModel() {
    loadModelFromUi(true);
}

bool MainWindow::ensureModelLoaded() {
    const QString config_path = resolvedPath(configPathEdit_->text().trimmed());
    if (engine_ && config_path == loadedConfigPath_) {
        return true;
    }
    return loadModelFromUi(false);
}

bool MainWindow::loadModelFromUi(bool show_success) {
    const QString config_path = resolvedPath(configPathEdit_->text().trimmed());
    if (config_path.isEmpty()) {
        QMessageBox::warning(this, "Load model", "Config path is empty.");
        return false;
    }

    try {
        WaitCursor cursor;
        yolo::AppConfig config = yolo::loadAppConfig(config_path.toStdString());
        auto engine = std::make_unique<yolo::YoloEngine>(config);

        config_ = std::move(config);
        engine_ = std::move(engine);
        loadedConfigPath_ = config_path;
        configPathEdit_->setText(config_path);

        const QString message = QString("Model loaded: %1").arg(
            QFileInfo(QString::fromStdString(config_.model_path)).fileName()
        );
        statusBar()->showMessage(message);
        if (show_success) {
            resultText_->setPlainText(message);
        }
        return true;
    } catch (const std::exception& e) {
        engine_.reset();
        loadedConfigPath_.clear();
        QMessageBox::critical(
            this,
            "Load model",
            QString("Failed to load model or config:\n%1").arg(QString::fromLocal8Bit(e.what()))
        );
        statusBar()->showMessage("Model load failed");
        return false;
    }
}

void MainWindow::runInference() {
    if (currentImage_.empty()) {
        QMessageBox::warning(this, "Run", "Open an image before running inference.");
        return;
    }
    if (!ensureModelLoaded()) {
        return;
    }

    const auto input = yolo::preprocessImageMat(currentImage_, config_);
    if (!input) {
        QMessageBox::warning(this, "Run", "Failed to preprocess the image.");
        return;
    }

    try {
        WaitCursor cursor;
        yolo::InferResult result = engine_->infer(*input);
        renderImage(currentImage_, result.detections);
        updateResultText(result);
        statusBar()->showMessage(
            QString("Done. %1 detections.").arg(
                static_cast<qulonglong>(result.detections.size())
            )
        );
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this,
            "Run",
            QString("Inference failed:\n%1").arg(QString::fromLocal8Bit(e.what()))
        );
        statusBar()->showMessage("Inference failed");
    }
}

QString MainWindow::defaultConfigPath() const {
    const QStringList candidates = {
        QDir::current().absoluteFilePath("yolo_onnx_cpp/config.yaml"),
        QDir::current().absoluteFilePath("../yolo_onnx_cpp/config.yaml"),
        QDir(QApplication::applicationDirPath()).absoluteFilePath("../yolo_onnx_cpp/config.yaml"),
        QDir(QApplication::applicationDirPath()).absoluteFilePath("../../yolo_onnx_cpp/config.yaml"),
    };

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists()) {
            return info.absoluteFilePath();
        }
    }
    return "yolo_onnx_cpp/config.yaml";
}

QString MainWindow::resolvedPath(const QString& path) const {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.isRelative()) {
        info = QFileInfo(QDir::current().absoluteFilePath(trimmed));
    }
    return info.absoluteFilePath();
}

QString MainWindow::detectionLabel(const yolo::Detection& detection) const {
    QString name = QString("class_%1").arg(detection.class_id);
    if (detection.class_id >= 0
        && detection.class_id < static_cast<int>(config_.class_names.size())) {
        name = QString::fromStdString(config_.class_names[static_cast<size_t>(detection.class_id)]);
    }
    return QString("%1 %2").arg(name).arg(detection.score, 0, 'f', 2);
}

cv::Mat MainWindow::drawDetections(
    const cv::Mat& image,
    const std::vector<yolo::Detection>& detections
) const {
    cv::Mat output = image.clone();
    if (output.empty()) {
        return output;
    }

    const int thickness = boxLineWidth(output);
    const double font_scale = std::max(0.5, static_cast<double>(thickness) * 0.35);

    for (const yolo::Detection& detection : detections) {
        const cv::Scalar color = classColor(detection.class_id);
        const cv::Point p1(
            std::clamp(static_cast<int>(std::round(detection.x1)), 0, output.cols - 1),
            std::clamp(static_cast<int>(std::round(detection.y1)), 0, output.rows - 1)
        );
        const cv::Point p2(
            std::clamp(static_cast<int>(std::round(detection.x2)), 0, output.cols - 1),
            std::clamp(static_cast<int>(std::round(detection.y2)), 0, output.rows - 1)
        );
        cv::rectangle(output, p1, p2, color, thickness);

        const std::string label = detectionLabel(detection).toStdString();
        int baseline = 0;
        const cv::Size label_size = cv::getTextSize(
            label,
            cv::FONT_HERSHEY_SIMPLEX,
            font_scale,
            thickness,
            &baseline
        );
        const int label_y = std::max(label_size.height + baseline + 4, p1.y);
        cv::rectangle(
            output,
            cv::Point(p1.x, label_y - label_size.height - baseline - 4),
            cv::Point(std::min(output.cols - 1, p1.x + label_size.width + 6), label_y),
            color,
            cv::FILLED
        );
        cv::putText(
            output,
            label,
            cv::Point(p1.x + 3, label_y - baseline - 2),
            cv::FONT_HERSHEY_SIMPLEX,
            font_scale,
            cv::Scalar(20, 20, 20),
            thickness,
            cv::LINE_AA
        );
    }

    return output;
}

void MainWindow::renderImage(
    const cv::Mat& image,
    const std::vector<yolo::Detection>& detections
) {
    renderedImage_ = matToQImage(drawDetections(image, detections));
    setImagePixmap();
}

void MainWindow::setImagePixmap() {
    if (!imageLabel_ || renderedImage_.isNull()) {
        return;
    }

    imageLabel_->setPixmap(
        QPixmap::fromImage(renderedImage_).scaled(
            imageLabel_->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        )
    );
}

void MainWindow::updateResultText(const yolo::InferResult& result) const {
    QStringList lines;
    lines << QString("Image: %1").arg(QFileInfo(currentImagePath_).fileName());
    lines << QString("Detections: %1").arg(
        static_cast<qulonglong>(result.detections.size())
    );
    lines << QString("Model inference: %1 ms").arg(result.model_inference_ms, 0, 'f', 2);
    lines << QString("Postprocess: %1 ms").arg(result.postprocess_ms, 0, 'f', 2);
    lines << "";

    for (int i = 0; i < static_cast<int>(result.detections.size()); ++i) {
        const yolo::Detection& detection = result.detections[static_cast<size_t>(i)];
        lines << QString("#%1  %2  box=(%3, %4, %5, %6)")
                     .arg(i + 1)
                     .arg(detectionLabel(detection))
                     .arg(detection.x1, 0, 'f', 1)
                     .arg(detection.y1, 0, 'f', 1)
                     .arg(detection.x2, 0, 'f', 1)
                     .arg(detection.y2, 0, 'f', 1);
    }

    resultText_->setPlainText(lines.join('\n'));
}

QImage MainWindow::matToQImage(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }

    cv::Mat converted;
    if (image.channels() == 1) {
        cv::cvtColor(image, converted, cv::COLOR_GRAY2RGB);
    } else if (image.channels() == 3) {
        cv::cvtColor(image, converted, cv::COLOR_BGR2RGB);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, converted, cv::COLOR_BGRA2RGBA);
    } else {
        return {};
    }

    const QImage::Format format = converted.channels() == 4
        ? QImage::Format_RGBA8888
        : QImage::Format_RGB888;

    return QImage(
        converted.data,
        converted.cols,
        converted.rows,
        static_cast<int>(converted.step),
        format
    ).copy();
}
