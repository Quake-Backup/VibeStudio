#include "app/material_library_model.h"

#include "app/studio_theme.h"
#include "core/material_render.h"
#include "core/render_device.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vibestudio {

namespace {

// How much memory swatches may hold; the rest are drawn again when seen.
constexpr qint64 kThumbnailBudgetBytes = 64LL * 1024 * 1024;
constexpr int kAnimationIntervalMs = 250;

QString shortName(const QString& name)
{
	// The last two path parts tell most materials apart in a grid.
	const QStringList parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	if (parts.size() <= 2) {
		return name;
	}
	return parts.mid(parts.size() - 2).join(QLatin1Char('/'));
}

// The theme colours a swatch is painted with, taken on the GUI thread when
// it is asked for.
struct SwatchColors {
	QColor blank;
	QColor warning;
};

// Marks a swatch with shapes, not colour alone: a triangle for animated
// materials, an exclamation for ones the engine drops or that have errors.
// Runs on the worker that drew the swatch; `side` is in device pixels.
QImage badged(const QImage& image, bool animated, bool problem, int side, qreal ratio, const SwatchColors& colors)
{
	QImage canvas = image.isNull() ? QImage(side, side, QImage::Format_ARGB32_Premultiplied) : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) {
		canvas.fill(colors.blank);
	}
	QPainter painter(&canvas);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const double badge = std::max(10.0 * ratio, canvas.width() / 5.0);
	const double margin = 3.0 * ratio;
	if (animated) {
		const QRectF box(canvas.width() - badge - margin, canvas.height() - badge - margin, badge, badge);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 150));
		painter.drawEllipse(box);
		const QPolygonF play {QPointF(box.left() + badge * 0.38, box.top() + badge * 0.28), QPointF(box.left() + badge * 0.38, box.bottom() - badge * 0.28),
			QPointF(box.right() - badge * 0.25, box.center().y())};
		painter.setBrush(Qt::white);
		painter.drawPolygon(play);
	}
	if (problem) {
		const QRectF box(margin, margin, badge, badge);
		const QPolygonF triangle {QPointF(box.center().x(), box.top()), QPointF(box.right(), box.bottom()), QPointF(box.left(), box.bottom())};
		painter.setPen(QPen(QColor(0, 0, 0, 180), ratio));
		painter.setBrush(colors.warning);
		painter.drawPolygon(triangle);
		painter.setPen(QPen(Qt::black, std::max(1.5 * ratio, badge / 8.0)));
		painter.drawLine(QPointF(box.center().x(), box.top() + badge * 0.35), QPointF(box.center().x(), box.bottom() - badge * 0.32));
		painter.drawPoint(QPointF(box.center().x(), box.bottom() - badge * 0.15));
	}
	painter.end();
	return canvas;
}

// The kind of a library entry as the user reads it; the identifiers are
// technical (doom-wall, quake2-wal).
QString entryKindName(const QString& kind)
{
	if (kind == QStringLiteral("shader")) {
		return MaterialLibraryModel::tr("shader");
	}
	if (kind == QStringLiteral("material")) {
		return MaterialLibraryModel::tr("material");
	}
	if (kind == QStringLiteral("guide")) {
		return MaterialLibraryModel::tr("material from a guide");
	}
	if (kind == QStringLiteral("implicit")) {
		return MaterialLibraryModel::tr("image without a script");
	}
	if (kind == QStringLiteral("doom-wall")) {
		return MaterialLibraryModel::tr("wall texture");
	}
	if (kind == QStringLiteral("doom-flat")) {
		return MaterialLibraryModel::tr("flat");
	}
	if (kind == QStringLiteral("quake-miptex")) {
		return MaterialLibraryModel::tr("texture");
	}
	if (kind == QStringLiteral("quake2-wal")) {
		return MaterialLibraryModel::tr("WAL texture");
	}
	return kind;
}

} // namespace

MaterialLibraryRows prepareMaterialLibraryRows(std::shared_ptr<const MaterialLibrary> library)
{
	MaterialLibraryRows rows;
	rows.library = std::move(library);
	if (!rows.library) {
		return rows;
	}
	rows.properties.reserve(rows.library->entries.size());
	for (int index = 0; index < rows.library->entries.size(); ++index) {
		rows.properties.push_back(materialLibraryEntryQueryProperties(rows.library->entries.at(index), rows.library->definition(index)));
	}
	return rows;
}

MaterialLibraryModel::MaterialLibraryModel(QObject* parent)
	: QAbstractListModel(parent)
{
	m_pool = new QThreadPool(this);
	m_pool->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 1, 4));
	m_pool->setObjectName(QStringLiteral("MaterialThumbnails"));
	m_animation = new QTimer(this);
	m_animation->setInterval(kAnimationIntervalMs);
	connect(m_animation, &QTimer::timeout, this, &MaterialLibraryModel::advanceAnimation);
	m_pending = std::make_shared<std::atomic_int>(0);
	m_swatchStop = std::make_shared<std::atomic_bool>(false);
	// OpenGL's surface is made on the GUI thread, before swatches draw.
	prepareRenderBackends();
}

MaterialLibraryModel::~MaterialLibraryModel()
{
	dropSwatchWork();
	m_pool->waitForDone();
}

void MaterialLibraryModel::dropSwatchWork()
{
	m_pool->clear();
	m_swatchStop->store(true);
	m_swatchStop = std::make_shared<std::atomic_bool>(false);
}

void MaterialLibraryModel::setRows(MaterialLibraryRows rows, const MaterialImageSource& images, std::shared_ptr<MaterialImageCache> cache)
{
	beginResetModel();
	dropSwatchWork();
	++m_generation;
	m_rows = std::move(rows);
	m_keys.clear();
	for (const StudioQueryProperties& properties : std::as_const(m_rows.properties)) {
		for (auto it = properties.keyBegin(); it != properties.keyEnd(); ++it) {
			m_keys.insert(*it);
		}
	}
	m_images = images;
	m_cache = std::move(cache);
	m_thumbnails.clear();
	m_requested.clear();
	m_visible.clear();
	endResetModel();
}

void MaterialLibraryModel::clear()
{
	setRows(MaterialLibraryRows(), MaterialImageSource(), nullptr);
}

const StudioQueryProperties& MaterialLibraryModel::properties(int row) const
{
	static const StudioQueryProperties empty;
	return row >= 0 && row < m_rows.properties.size() ? m_rows.properties.at(row) : empty;
}

int MaterialLibraryModel::rowForName(const QString& name, MaterialEngine engine) const
{
	return m_rows.library ? m_rows.library->indexOf(name, engine) : -1;
}

void MaterialLibraryModel::setThumbnailSide(int side)
{
	side = std::clamp(side, 24, 256);
	if (side == m_side) {
		return;
	}
	m_side = side;
	refreshThumbnails();
}

void MaterialLibraryModel::refreshThumbnails()
{
	dropSwatchWork();
	++m_generation;
	m_thumbnails.clear();
	m_requested.clear();
	// The blank swatch is painted in the theme's colour too.
	m_placeholder = QPixmap();
	if (!m_rows.library || m_rows.library->entries.isEmpty()) {
		return;
	}
	Q_EMIT dataChanged(index(0), index(rowCount() - 1), {Qt::DecorationRole});
}

void MaterialLibraryModel::setDevicePixelRatio(qreal ratio)
{
	ratio = std::clamp(ratio, 1.0, 4.0);
	if (qFuzzyCompare(ratio, m_devicePixelRatio)) {
		return;
	}
	m_devicePixelRatio = ratio;
	refreshThumbnails();
}

int MaterialLibraryModel::thumbnailBudget() const
{
	const qint64 pixels = static_cast<qint64>(std::lround(m_side * m_devicePixelRatio));
	return static_cast<int>(std::clamp<qint64>(kThumbnailBudgetBytes / std::max<qint64>(1, pixels * pixels * 4), 64, 4000));
}

void MaterialLibraryModel::setItemSize(const QSize& size)
{
	if (size == m_itemSize) {
		return;
	}
	m_itemSize = size;
	if (rowCount() > 0) {
		Q_EMIT dataChanged(index(0), index(rowCount() - 1), {Qt::SizeHintRole});
	}
}

void MaterialLibraryModel::setAnimateThumbnails(bool animate)
{
	m_animate = animate;
	if (animate) {
		m_animation->start();
	} else {
		m_animation->stop();
	}
}

void MaterialLibraryModel::setVisibleRows(const QVector<int>& rows)
{
	m_visible = rows;
	// Swatches queued for rows that scrolled away are not worth drawing.
	if (m_requested.size() > rows.size() * 2 + 16) {
		m_pool->clear();
		m_requested.clear();
	}
	if (m_thumbnails.size() > thumbnailBudget()) {
		const QSet<int> keep(rows.cbegin(), rows.cend());
		for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();) {
			it = keep.contains(it.key()) ? std::next(it) : m_thumbnails.erase(it);
		}
	}
}

int MaterialLibraryModel::pendingThumbnails() const
{
	return static_cast<int>(m_requested.size());
}

int MaterialLibraryModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() || !m_rows.library ? 0 : static_cast<int>(m_rows.library->entries.size());
}

QString MaterialLibraryModel::rowDescription(int row) const
{
	const MaterialLibraryEntry& entry = m_rows.library->entries.at(row);
	QStringList traits;
	if (entry.animated) {
		traits << tr("animated");
	}
	if (entry.sky) {
		traits << tr("sky");
	}
	if (entry.light) {
		traits << tr("light");
	}
	if (entry.fog) {
		traits << tr("fog");
	}
	if (entry.translucent) {
		traits << tr("translucent");
	}
	if (entry.rejected) {
		traits << tr("dropped by the engine");
	}
	if (entry.shadowed) {
		traits << tr("shadowed by %1").arg(entry.shadowedBy);
	}
	if (entry.base) {
		traits << tr("from the base game, read only");
	}
	if (entry.errors > 0) {
		traits << tr("%n error(s)", nullptr, entry.errors);
	}
	if (entry.warnings > 0) {
		traits << tr("%n warning(s)", nullptr, entry.warnings);
	}
	QString text = tr("%1, %2 %3").arg(entry.name, materialEngineDisplayName(entry.engine), entryKindName(entry.kind));
	if (!traits.isEmpty()) {
		text += QStringLiteral(", ") + traits.join(QStringLiteral(", "));
	}
	return text;
}

QVariant MaterialLibraryModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || !m_rows.library || index.row() >= m_rows.library->entries.size()) {
		return {};
	}
	const int row = index.row();
	const MaterialLibraryEntry& entry = m_rows.library->entries.at(row);
	switch (role) {
	case Qt::DisplayRole:
		if (entry.shadowed) {
			return tr("%1 (shadowed)").arg(shortName(entry.name));
		}
		return entry.base ? tr("%1 (base)").arg(shortName(entry.name)) : shortName(entry.name);
	case Qt::ToolTipRole: {
		QString tip = rowDescription(row);
		if (!entry.sourcePath.isEmpty()) {
			tip += QLatin1Char('\n') + (entry.line > 0 ? tr("%1, line %2").arg(entry.sourcePath).arg(entry.line) : entry.sourcePath);
		}
		return tip;
	}
	case Qt::AccessibleTextRole:
		return rowDescription(row);
	case Qt::AccessibleDescriptionRole:
		return entry.line > 0 ? tr("%1, line %2").arg(entry.sourcePath).arg(entry.line) : entry.sourcePath;
	case Qt::DecorationRole: {
		const auto found = m_thumbnails.constFind(row);
		if (found != m_thumbnails.constEnd()) {
			return found.value();
		}
		requestThumbnail(row, m_time);
		// A blank swatch of the final size while it draws, so the view lays
		// out icon space from the start.
		const int pixels = static_cast<int>(std::lround(m_side * m_devicePixelRatio));
		if (m_placeholder.isNull() || m_placeholder.width() != pixels) {
			m_placeholder = QPixmap(pixels, pixels);
			m_placeholder.fill(currentStudioTheme().colors.panelRaised);
			m_placeholder.setDevicePixelRatio(m_devicePixelRatio);
		}
		return m_placeholder;
	}
	case Qt::SizeHintRole:
		return m_itemSize.isValid() ? QVariant(m_itemSize) : QVariant();
	case Qt::ForegroundRole:
		if (entry.shadowed || entry.base) {
			return currentStudioTheme().colors.textMuted;
		}
		return {};
	case EntryRole:
		return row;
	case NameRole:
		return entry.name;
	case EngineRole:
		return static_cast<int>(entry.engine);
	case KindRole:
		return entry.kind;
	case AnimatedRole:
		return entry.animated;
	case ShadowedRole:
		return entry.shadowed;
	case BaseRole:
		return entry.base;
	case ProblemsRole:
		return entry.errors * 1000 + entry.warnings + (entry.rejected ? 1000000 : 0);
	case SourceRole:
		return entry.sourcePath;
	default:
		return {};
	}
}

void MaterialLibraryModel::requestThumbnail(int row, double time) const
{
	if (m_requested.contains(row) || !m_rows.library) {
		return;
	}
	m_requested.insert(row);
	const std::shared_ptr<const MaterialLibrary> library = m_rows.library;
	const MaterialImageSource images = m_images;
	const std::shared_ptr<MaterialImageCache> cache = m_cache;
	const quint64 generation = m_generation;
	const qreal ratio = m_devicePixelRatio;
	// Drawn at the screen's pixel density; the badges too, on the worker,
	// in the theme's colours as they are now.
	const int side = static_cast<int>(std::lround(m_side * ratio));
	const StudioThemeColors& theme = currentStudioTheme().colors;
	const SwatchColors colors {theme.panelRaised, theme.warning};
	const std::shared_ptr<std::atomic_bool> stop = m_swatchStop;
	QPointer<MaterialLibraryModel> self(const_cast<MaterialLibraryModel*>(this));
	m_pool->start([library, images, cache, generation, side, ratio, colors, stop, row, time, self]() {
		if (stop->load()) {
			return;
		}
		QImage image;
		bool animated = false;
		bool problem = false;
		if (row < library->entries.size()) {
			const MaterialLibraryEntry& entry = library->entries.at(row);
			animated = entry.animated;
			problem = entry.rejected || entry.errors > 0;
			const MaterialDefinition definition = library->definition(row);
			const MaterialImageSet set = resolveMaterialImages(definition, images);
			image = renderMaterialSwatch(definition, set, library->tables, side, time, {}, stop.get()).image;
		}
		if (stop->load()) {
			return;
		}
		image = badged(image, animated, problem, side, ratio, colors);
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, generation, row, image, ratio]() {
				if (self) {
					self->deliverThumbnail(generation, row, image, ratio);
				}
			},
			Qt::QueuedConnection);
	});
}

void MaterialLibraryModel::deliverThumbnail(quint64 generation, int row, const QImage& image, qreal ratio)
{
	if (generation != m_generation || !m_rows.library || row >= m_rows.library->entries.size()) {
		return;
	}
	m_requested.remove(row);
	QPixmap pixmap = QPixmap::fromImage(image);
	pixmap.setDevicePixelRatio(ratio);
	m_thumbnails.insert(row, pixmap);
	const QModelIndex changed = index(row);
	Q_EMIT dataChanged(changed, changed, {Qt::DecorationRole});
}

void MaterialLibraryModel::advanceAnimation()
{
	if (!m_animate || !m_rows.library) {
		return;
	}
	m_time += kAnimationIntervalMs / 1000.0;
	for (int row : std::as_const(m_visible)) {
		if (row >= 0 && row < m_rows.library->entries.size() && m_rows.library->entries.at(row).animated) {
			requestThumbnail(row, m_time);
		}
	}
}

MaterialLibraryFilter::MaterialLibraryFilter(QObject* parent)
	: QSortFilterProxyModel(parent)
{
	setDynamicSortFilter(false);
}

void MaterialLibraryFilter::refilter(const std::function<void()>& change)
{
	// Qt 6.10 replaced invalidateFilter() with a bracketed change.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	beginFilterChange();
	change();
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
	change();
	invalidateFilter();
#endif
}

void MaterialLibraryFilter::setQuery(const QString& text)
{
	refilter([&]() {
		m_text = text;
		m_query = parseStudioQuery(text);
	});
}

void MaterialLibraryFilter::setEngine(MaterialEngine engine)
{
	refilter([&]() { m_engine = engine; });
}

void MaterialLibraryFilter::setShowShadowed(bool show)
{
	refilter([&]() { m_showShadowed = show; });
}

QStringList MaterialLibraryFilter::unknownKeys() const
{
	const auto* model = qobject_cast<const MaterialLibraryModel*>(sourceModel());
	return model ? studioQueryUnknownKeys(m_query, model->propertyKeys()) : QStringList();
}

bool MaterialLibraryFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	const auto* model = qobject_cast<const MaterialLibraryModel*>(sourceModel());
	if (!model || !model->library() || sourceRow >= model->library()->entries.size()) {
		return false;
	}
	Q_UNUSED(sourceParent);
	const MaterialLibraryEntry& entry = model->library()->entries.at(sourceRow);
	if (m_engine != MaterialEngine::Unknown && entry.engine != m_engine) {
		return false;
	}
	if (!m_showShadowed && entry.shadowed) {
		return false;
	}
	return m_query.isEmpty() || studioQueryMatches(m_query, model->properties(sourceRow), entry.name);
}

void MaterialLibraryFilter::setOrder(Order order)
{
	m_order = order;
	// Column -1 puts the rows back in the library's own order.
	sort(order == Order::Library ? -1 : 0);
}

void MaterialLibraryFilter::setUseCounts(const QHash<int, int>& counts)
{
	m_useCounts = counts;
	if (m_order == Order::MostUsed) {
		sort(-1);
		sort(0);
	}
}

bool MaterialLibraryFilter::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
	const auto byName = [&]() {
		return QString::localeAwareCompare(left.data(MaterialLibraryModel::NameRole).toString(), right.data(MaterialLibraryModel::NameRole).toString()) < 0;
	};
	switch (m_order) {
	case Order::Library:
		return left.row() < right.row();
	case Order::Name:
		return byName();
	case Order::Engine: {
		const int a = left.data(MaterialLibraryModel::EngineRole).toInt();
		const int b = right.data(MaterialLibraryModel::EngineRole).toInt();
		return a != b ? a < b : byName();
	}
	case Order::Source: {
		const int compared = QString::compare(left.data(MaterialLibraryModel::SourceRole).toString(),
			right.data(MaterialLibraryModel::SourceRole).toString(), Qt::CaseInsensitive);
		return compared != 0 ? compared < 0 : left.row() < right.row();
	}
	case Order::Problems: {
		// Most serious first.
		const int a = left.data(MaterialLibraryModel::ProblemsRole).toInt();
		const int b = right.data(MaterialLibraryModel::ProblemsRole).toInt();
		return a != b ? a > b : byName();
	}
	case Order::MostUsed: {
		const int a = m_useCounts.value(left.row());
		const int b = m_useCounts.value(right.row());
		return a != b ? a > b : byName();
	}
	}
	return false;
}

} // namespace vibestudio
