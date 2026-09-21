#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <algorithm>

namespace {
QString displayName(QString name) {
    name.replace(QRegularExpression("[-_]+"), " ");
    const auto words = name.split(' ', Qt::SkipEmptyParts);
    QStringList titled;
    titled.reserve(words.size());
    for (QString word : words) {
        if (!word.isEmpty())
            word[0] = word[0].toUpper();
        titled.push_back(word);
    }
    return titled.join(' ');
}

QVariantMap loadPalette(const QString &path) {
    QVariantMap palette{
        {"primary", "#8dd5b3"},
        {"onPrimary", "#003221"},
        {"primaryContainer", "#1c684b"},
        {"onPrimaryContainer", "#ffffff"},
        {"surface", "#0f1511"},
        {"surfaceContainer", "#1e2320"},
        {"surfaceContainerHigh", "#282e2a"},
        {"onSurface", "#dee4de"},
        {"onSurfaceVariant", "#bfc9c2"},
        {"outline", "#8a938c"},
        {"outlineVariant", "#555e58"}
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return palette;

    static const QHash<QString, QString> keys{
        {"primary", "primary"}, {"on-primary", "onPrimary"},
        {"primary-container", "primaryContainer"},
        {"on-primary-container", "onPrimaryContainer"},
        {"surface", "surface"}, {"surface-container", "surfaceContainer"},
        {"surface-container-high", "surfaceContainerHigh"},
        {"on-surface", "onSurface"},
        {"on-surface-variant", "onSurfaceVariant"},
        {"outline", "outline"}, {"outline-variant", "outlineVariant"}
    };
    const QRegularExpression token(R"(^\s*([a-z-]+):\s*(#[0-9a-fA-F]{6,8});)");
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto match = token.match(stream.readLine());
        if (match.hasMatch() && keys.contains(match.captured(1)))
            palette[keys.value(match.captured(1))] = match.captured(2);
    }
    return palette;
}

QString thumbnailFor(const QString &path, const QString &cacheDir) {
    const QFileInfo source(path);
    QByteArray signature = source.canonicalFilePath().toUtf8();
    signature.append('\0');
    signature.append(QByteArray::number(source.size()));
    signature.append(':');
    signature.append(QByteArray::number(source.lastModified().toMSecsSinceEpoch()));
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex().left(24));
    const QString thumbnail = cacheDir + '/' + digest + ".jpg";
    if (QFileInfo(thumbnail).size() > 0)
        return thumbnail;

    QImageReader reader(path);
    reader.setAllocationLimit(512);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid())
        reader.setScaledSize(sourceSize.scaled(QSize(768, 432), Qt::KeepAspectRatioByExpanding));

    QImage image = reader.read();
    if (image.isNull())
        return path;
    image = image.scaled(768, 432, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int left = std::max(0, (image.width() - 768) / 2);
    const int top = std::max(0, (image.height() - 432) / 2);
    image = image.copy(left, top, std::min(768, image.width()), std::min(432, image.height()));

    const QString temporary = thumbnail + ".tmp.jpg";
    if (!image.save(temporary, "JPG", 84))
        return path;
    QFile::remove(thumbnail);
    if (!QFile::rename(temporary, thumbnail)) {
        QFile::remove(temporary);
        return path;
    }
    return thumbnail;
}
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName("Wallpaper Gallery");
    QGuiApplication::setDesktopFileName("wallpaper-overview");

    const QString home = QDir::homePath();
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QStringList extensions{"*.jpg", "*.jpeg", "*.png", "*.webp", "*.bmp", "*.gif"};
    const QString thumbnailDir = home + "/.cache/mango-theme/wallpaper-thumbnails";
    QDir().mkpath(thumbnailDir);

    struct Entry {
        QString path;
        QString name;
        QString category;
        QString detail;
    };
    QList<Entry> scanned;
    QDirIterator iterator(pictures, extensions, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    const QDir pictureDir(pictures);
    while (iterator.hasNext()) {
        const QString path = QFileInfo(iterator.next()).canonicalFilePath();
        if (path.isEmpty())
            continue;

        const QString relative = pictureDir.relativeFilePath(path);
        const QStringList parts = relative.split('/', Qt::SkipEmptyParts);
        QString category = "Personal";
        QString detail = "Local collection";
        if (parts.size() >= 3 && parts.first() == "Wallpapers") {
            category = parts.value(1);
            detail = parts.size() >= 4 ? parts.value(2) : "Collection";
        } else if (parts.size() >= 2) {
            category = parts.first();
            detail = "Collection";
        }

        scanned.push_back({path, displayName(QFileInfo(path).completeBaseName()), category, detail});
    }

    std::sort(scanned.begin(), scanned.end(), [](const Entry &a, const Entry &b) {
        const int categoryOrder = QString::localeAwareCompare(a.category, b.category);
        return categoryOrder == 0 ? QString::localeAwareCompare(a.name, b.name) < 0 : categoryOrder < 0;
    });

    QString currentPath;
    QFile currentFile(home + "/.cache/mango-theme/wallpaper");
    if (currentFile.open(QIODevice::ReadOnly | QIODevice::Text))
        currentPath = QFileInfo(QString::fromUtf8(currentFile.readAll()).trimmed()).canonicalFilePath();

    QVariantList wallpapers;
    QStringList categories{"All"};
    QSet<QString> seenCategories;
    int initialIndex = 0;
    wallpapers.reserve(scanned.size());
    for (qsizetype index = 0; index < scanned.size(); ++index) {
        const auto &entry = scanned.at(index);
        QVariantMap value{
            {"path", entry.path},
            {"thumbnailUrl", QUrl::fromLocalFile(thumbnailFor(entry.path, thumbnailDir))},
            {"name", entry.name},
            {"category", entry.category},
            {"detail", entry.detail},
            {"search", (entry.name + ' ' + entry.category + ' ' + entry.detail).toLower()}
        };
        wallpapers.push_back(value);
        if (!seenCategories.contains(entry.category)) {
            seenCategories.insert(entry.category);
            categories.push_back(entry.category);
        }
        if (entry.path == currentPath)
            initialIndex = static_cast<int>(index);
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("wallpaperEntries", wallpapers);
    engine.rootContext()->setContextProperty("categoryNames", categories);
    engine.rootContext()->setContextProperty("initialWallpaperIndex", initialIndex);
    engine.rootContext()->setContextProperty("themeColors", loadPalette(home + "/.config/rofi/colors.rasi"));

    const QString qmlPath = home + "/.local/share/wallpaper-overview/Main.qml";
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().constFirst();
    const int status = app.exec();
    const QString selectedPath = root->property("selectedPath").toString();
    if (!selectedPath.isEmpty())
        QProcess::startDetached(home + "/.local/bin/theme-wallpaper", {selectedPath});
    return status;
}
