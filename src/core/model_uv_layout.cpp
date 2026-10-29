#include "core/model_uv_layout.h"

#include "core/model_uv.h"
#include "core/model_uv_atlas.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvLayout)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace

QVector<int> modelUvLayoutMaterialSurfaces(const ModelMesh &mesh, int surface)
{
	QVector<int> result;
	if (surface < 0 || surface >= mesh.surfaces.size())
		return result;
	result.append(surface);
	for (int s = 0; s < mesh.surfaces.size(); ++s)
		if (s != surface && modelUvSurfacesShareMaterial(mesh.surfaces[surface], mesh.surfaces[s]))
			result.append(s);
	return result;
}

bool renderModelUvLayout(const ModelMesh &mesh, const QVector<int> &surfaces, QSize size, const ModelUvLayoutOptions &options, QImage *image,
						 ModelUvLayoutReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!image || surfaces.isEmpty() || size.width() < 1 || size.height() < 1 || size.width() > modelUvLayoutMaxSize ||
		size.height() > modelUvLayoutMaxSize || !std::isfinite(options.lineWidth) || options.lineWidth <= 0 || options.lineWidth > 64 ||
		!std::isfinite(options.fillOpacity) || options.fillOpacity < 0 || options.fillOpacity > 1)
		return fail(error, Text::tr("Choose at least one surface, a layout size from 1 to %1 pixels per side, a line width above 0 and up "
									"to 64, and a fill opacity from 0 to 1.")
							   .arg(modelUvLayoutMaxSize));
	for (int s : surfaces)
		if (s < 0 || s >= mesh.surfaces.size() || surfaces.count(s) != 1)
			return fail(error, Text::tr("Choose each existing surface once."));
	const int width = size.width(), height = size.height();
	QImage result(width, height, QImage::Format_ARGB32);
	if (result.isNull())
		return fail(error, Text::tr("Unable to allocate the UV layout image. Choose a smaller size."));
	result.fill(options.background);
	ModelUvLayoutReport summary;
	summary.size = size;
	summary.surfaces = int(surfaces.size());
	QPainter painter(&result);
	painter.setRenderHint(QPainter::Antialiasing, options.antialias);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, options.antialias);
	if (!options.underlay.isNull())
		painter.drawImage(QRectF(0, 0, width, height), options.underlay);
	const auto pixel = [&](ModelTexCoord uv, QPointF shift) { return QPointF(double(uv.u) * width + shift.x(), double(uv.v) * height + shift.y()); };
	for (int s : surfaces)
	{
		const auto &surface = mesh.surfaces[s];
		ModelUvTopology topology;
		if (!buildModelUvTopology(surface, &topology, error, control))
			return false;
		// Each face's whole-tile shift: none, or (with wrap) the tiles that
		// bring its centre into 0–1.
		QVector<QPointF> shift(surface.triangles.size());
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			if (!work.step())
				return false;
			const auto t = surface.triangles[face];
			bool outside = false;
			double cu = 0, cv = 0;
			for (int v : {t.a, t.b, t.c})
			{
				const auto uv = surface.texCoords[v];
				outside = outside || uv.u < 0 || uv.u > 1 || uv.v < 0 || uv.v > 1;
				cu += uv.u / 3.0;
				cv += uv.v / 3.0;
			}
			summary.outsideFaces += outside;
			if (outside && options.wrap)
			{
				const double tu = std::floor(cu), tv = std::floor(cv);
				if (tu != 0 || tv != 0)
				{
					shift[face] = QPointF(-tu * width, -tv * height);
					++summary.wrappedFaces;
				}
			}
		}
		summary.faces += int(surface.triangles.size());
		summary.islands += int(topology.islands.size());
		if (options.fillOpacity > 0)
		{
			painter.setPen(Qt::NoPen);
			for (int island = 0; island < topology.islands.size(); ++island)
			{
				QPainterPath path;
				path.setFillRule(Qt::WindingFill);
				for (int face : topology.islands[island].faces)
				{
					if (!work.step())
						return false;
					const auto t = surface.triangles[face];
					QPolygonF polygon;
					for (int v : {t.a, t.b, t.c})
						polygon << pixel(surface.texCoords[v], shift[face]);
					// Each face winds the same way in the path, so overlapping or
					// mirrored faces still fill the island once.
					if ((polygon[1].x() - polygon[0].x()) * (polygon[2].y() - polygon[0].y()) -
							(polygon[1].y() - polygon[0].y()) * (polygon[2].x() - polygon[0].x()) <
						0)
						std::swap(polygon[1], polygon[2]);
					path.addPolygon(polygon);
					path.closeSubpath();
				}
				QColor colour = options.islandTints ? QColor::fromHsvF(std::fmod(island * 0.618033988749895 + s * 0.31, 1.0), 0.55, 0.95)
													: options.fill;
				colour.setAlphaF(float(options.fillOpacity * colour.alphaF()));
				painter.setBrush(colour);
				painter.drawPath(path);
			}
		}
		// Ordinary edges first, island boundaries above them.
		for (int pass = 0; pass < 2; ++pass)
		{
			QPen pen(pass ? options.seam : options.edge, options.lineWidth);
			pen.setCapStyle(Qt::SquareCap);
			pen.setJoinStyle(Qt::MiterJoin);
			painter.setPen(pen);
			painter.setBrush(Qt::NoBrush);
			for (const auto &edge : topology.edges)
			{
				if (!work.step())
					return false;
				const bool boundary = edge.seam || edge.faces.size() != 2 ||
									  topology.faceIsland[edge.faces[0]] != topology.faceIsland[edge.faces[1]];
				if ((pass == 1) != (boundary && options.seams))
					continue;
				for (int face : edge.faces)
				{
					// Shared edges draw once unless the faces sit in different tiles.
					if (face != edge.faces.first() && shift[face] == shift[edge.faces.first()])
						continue;
					painter.drawLine(pixel(surface.texCoords[edge.vertices.first], shift[face]),
									 pixel(surface.texCoords[edge.vertices.second], shift[face]));
				}
			}
		}
	}
	painter.end();
	if (!work.check())
		return false;
	*image = std::move(result);
	if (report)
		*report = summary;
	return true;
}

QJsonObject modelUvLayoutJson(const ModelUvLayoutReport &report)
{
	return {{"width", report.size.width()},	  {"height", report.size.height()},		  {"surfaces", report.surfaces},
			{"faces", report.faces},		  {"islands", report.islands},			  {"outsideFaces", report.outsideFaces},
			{"wrappedFaces", report.wrappedFaces}};
}
} // namespace vibestudio
