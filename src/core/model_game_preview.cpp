#include "core/model_game_preview.h"

#include "core/material_eval.h"

#include <QCoreApplication>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kTopRange = 16;
constexpr int kBottomRange = 96;
constexpr int kFirstFullbright = 224;

struct Rgba {
	double r = 0, g = 0, b = 0, a = 0;
};

// Wrapped or clamped bilinear (or point) sample of a straight-alpha texture.
Rgba sampleTexture(const MaterialTexture& texture, double s, double t, bool clamp, bool nearest)
{
	const int w = texture.width, h = texture.height;
	const auto texel = [&](int x, int y) {
		if (clamp) {
			x = std::clamp(x, 0, w - 1);
			y = std::clamp(y, 0, h - 1);
		} else {
			x = ((x % w) + w) % w;
			y = ((y % h) + h) % h;
		}
		const QRgb p = texture.pixels.at(qsizetype(y) * w + x);
		return Rgba{qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0, qAlpha(p) / 255.0};
	};
	const double x = s * w - 0.5, y = t * h - 0.5;
	if (nearest) {
		return texel(int(std::floor(s * w)), int(std::floor(t * h)));
	}
	const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
	const double fx = x - x0, fy = y - y0;
	const Rgba a = texel(x0, y0), b = texel(x0 + 1, y0), c = texel(x0, y0 + 1), d = texel(x0 + 1, y0 + 1);
	const auto mix = [&](double pa, double pb, double pc, double pd) {
		return (pa * (1 - fx) + pb * fx) * (1 - fy) + (pc * (1 - fx) + pd * fx) * fy;
	};
	return Rgba{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
}

// Quake III's tcMod evaluation (tr_shade_calc.c RB_Calc*TexCoords), applied
// in the order written. Turb needs a vertex position; the texture position
// stands in for it.
void applyTexMods(const QVector<MaterialTexMod>& mods, double time, double* s, double* t)
{
	for (const MaterialTexMod& mod : mods) {
		switch (mod.kind) {
		case MaterialTexModKind::Scroll: {
			double ds = mod.values[0] * time, dt = mod.values[1] * time;
			ds -= std::floor(ds);
			dt -= std::floor(dt);
			*s += ds;
			*t += dt;
			break;
		}
		case MaterialTexModKind::Scale:
			*s *= mod.values[0];
			*t *= mod.values[1];
			break;
		case MaterialTexModKind::Rotate: {
			const double radians = -mod.values[0] * time * kPi / 180.0;
			const double sine = std::sin(radians), cosine = std::cos(radians);
			const double s0 = *s, t0 = *t;
			*s = s0 * cosine + t0 * -sine + (0.5 - 0.5 * cosine + 0.5 * sine);
			*t = s0 * sine + t0 * cosine + (0.5 - 0.5 * sine - 0.5 * cosine);
			break;
		}
		case MaterialTexModKind::Stretch: {
			const double wave = evaluateQuake3Wave(mod.wave, time);
			const double p = std::abs(wave) > 1e-9 ? 1.0 / wave : 1.0;
			*s = *s * p + (0.5 - 0.5 * p);
			*t = *t * p + (0.5 - 0.5 * p);
			break;
		}
		case MaterialTexModKind::Transform: {
			const double s0 = *s, t0 = *t;
			*s = s0 * mod.values[0] + t0 * mod.values[2] + mod.values[4];
			*t = s0 * mod.values[1] + t0 * mod.values[3] + mod.values[5];
			break;
		}
		case MaterialTexModKind::Turbulent: {
			const double now = mod.wave.phase + time * mod.wave.frequency;
			const double s0 = *s, t0 = *t;
			*s += std::sin(((s0 * 64.0) / 128.0 * 0.125 + now) * 2.0 * kPi) * mod.wave.amplitude;
			*t += std::sin(((t0 * 64.0) / 128.0 * 0.125 + now) * 2.0 * kPi) * mod.wave.amplitude;
			break;
		}
		default:
			break;
		}
	}
}

double factor(MaterialBlendFactor kind, double source, double sourceAlpha, double destination, double destinationAlpha)
{
	switch (kind) {
	case MaterialBlendFactor::Zero:
		return 0.0;
	case MaterialBlendFactor::One:
		return 1.0;
	case MaterialBlendFactor::SourceColor:
		return source;
	case MaterialBlendFactor::OneMinusSourceColor:
		return 1.0 - source;
	case MaterialBlendFactor::DestinationColor:
		return destination;
	case MaterialBlendFactor::OneMinusDestinationColor:
		return 1.0 - destination;
	case MaterialBlendFactor::SourceAlpha:
		return sourceAlpha;
	case MaterialBlendFactor::OneMinusSourceAlpha:
		return 1.0 - sourceAlpha;
	case MaterialBlendFactor::DestinationAlpha:
		return destinationAlpha;
	case MaterialBlendFactor::OneMinusDestinationAlpha:
		return 1.0 - destinationAlpha;
	case MaterialBlendFactor::SourceAlphaSaturate:
		return std::min(sourceAlpha, 1.0 - destinationAlpha);
	}
	return 0.0;
}

} // namespace

void modelQuakeFloodFill(QByteArray* indices, int width, int height, int black)
{
	// GLQuake gl_model.c Mod_FloodFillSkin (GPL-2.0-or-later), behaviour only.
	if (!indices || width <= 0 || height <= 0 || indices->size() < qsizetype(width) * height) {
		return;
	}
	auto* skin = reinterpret_cast<quint8*>(indices->data());
	const int fill = skin[0];
	if (fill == black || fill == 255) {
		return;
	}
	QVector<QPair<int, int>> queue;
	queue.append({0, 0});
	for (int next = 0; next < queue.size(); ++next) {
		const int x = queue.at(next).first, y = queue.at(next).second;
		int colour = black;
		const auto step = [&](int nx, int ny) {
			quint8& texel = skin[qsizetype(ny) * width + nx];
			if (texel == fill) {
				texel = 255;
				queue.append({nx, ny});
			} else if (texel != 255) {
				colour = texel;
			}
		};
		if (x > 0) {
			step(x - 1, y);
		}
		if (x < width - 1) {
			step(x + 1, y);
		}
		if (y > 0) {
			step(x, y - 1);
		}
		if (y < height - 1) {
			step(x, y + 1);
		}
		skin[qsizetype(y) * width + x] = quint8(colour);
	}
}

QByteArray modelQuakePlayerTranslation(int topColor, int bottomColor)
{
	// Quake's CL_NewTranslation / R_TranslatePlayerSkin (GPL-2.0-or-later):
	// a row below 8 runs bright to dark like the original ranges, a later
	// row the other way.
	QByteArray table(256, '\0');
	for (int index = 0; index < 256; ++index) {
		table[index] = char(index);
	}
	const auto translate = [&table](int range, int row) {
		if (row < 0) {
			return;
		}
		const int base = std::clamp(row, 0, 15) * 16;
		for (int i = 0; i < 16; ++i) {
			table[range + i] = char(base < 128 ? base + i : base + 15 - i);
		}
	};
	translate(kTopRange, topColor);
	translate(kBottomRange, bottomColor);
	return table;
}

bool composeModelQuakeSkin(const ModelMesh& mesh, int skin, const ModelGamePreviewOptions& options, ModelGameSkin* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!out) {
		return false;
	}
	*out = {};
	const int width = mesh.mdl.skinSize.width(), height = mesh.mdl.skinSize.height();
	if (skin < 0 || skin >= mesh.embeddedSkins.size() || mesh.embeddedSkins.at(skin).indexedFrames.isEmpty() || width <= 0 || height <= 0
		|| mesh.mdl.palette.size() < 768) {
		return fail(QCoreApplication::translate("VibeStudioModelGamePreview", "The model has no indexed Quake skin %1 to preview.").arg(skin));
	}
	const ModelEmbeddedSkin& source = mesh.embeddedSkins.at(skin);
	// R_AliasSetupSkin: the member whose cumulative end time is past the time
	// within the group's cycle.
	int member = 0;
	if (!source.intervals.isEmpty() && source.intervals.last() > 0.0f) {
		const double cycle = source.intervals.last();
		const double within = options.time - std::floor(options.time / cycle) * cycle;
		while (member < source.intervals.size() - 1 && source.intervals.at(member) <= within) {
			++member;
		}
		out->frame = member;
	}
	member = std::min(member, int(source.indexedFrames.size()) - 1);
	QByteArray indices = source.indexedFrames.at(member);
	if (indices.size() != qsizetype(width) * height) {
		return fail(QCoreApplication::translate("VibeStudioModelGamePreview", "Skin %1 does not match the model's skin size.").arg(skin));
	}
	const auto* palette = reinterpret_cast<const quint8*>(mesh.mdl.palette.constData());
	const bool software = options.quakeRenderer == MaterialQuakeRenderer::Software;
	if (options.floodFill && !software) {
		int black = 0;
		for (int index = 0; index < 256; ++index) {
			if (palette[index * 3] == 0 && palette[index * 3 + 1] == 0 && palette[index * 3 + 2] == 0) {
				black = index;
				break;
			}
		}
		modelQuakeFloodFill(&indices, width, height, black);
	}
	const QByteArray translation = modelQuakePlayerTranslation(options.topColor, options.bottomColor);
	const bool fullbrights = options.quakeRenderer != MaterialQuakeRenderer::GLQuake;
	out->base = QImage(width, height, QImage::Format_ARGB32);
	QImage glow;
	for (int y = 0; y < height; ++y) {
		auto* line = reinterpret_cast<QRgb*>(out->base.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const int index = quint8(translation.at(quint8(indices.at(qsizetype(y) * width + x))));
			const QRgb colour = qRgb(palette[index * 3], palette[index * 3 + 1], palette[index * 3 + 2]);
			if (fullbrights && index >= kFirstFullbright) {
				if (glow.isNull()) {
					glow = QImage(width, height, QImage::Format_ARGB32);
					glow.fill(qRgba(0, 0, 0, 255));
				}
				glow.setPixel(x, y, colour);
				line[x] = qRgb(0, 0, 0);
			} else {
				line[x] = colour;
			}
		}
	}
	out->glow = glow;
	out->nearest = software;
	if (software) {
		out->notes << QCoreApplication::translate("VibeStudioModelGamePreview",
			"Software renderer: point sampled, fullbright texels unlit; the colormap's light levels are approximated by the view's shade.");
	} else if (options.quakeRenderer == MaterialQuakeRenderer::GLQuake) {
		out->notes << QCoreApplication::translate("VibeStudioModelGamePreview", "GLQuake: bilinear, and fullbright texels are lit like the rest.");
	}
	if (options.topColor >= 0 || options.bottomColor >= 0) {
		out->notes << QCoreApplication::translate("VibeStudioModelGamePreview", "Player colours translate indices 16-31 (top) and 96-111 (bottom).");
	}
	return true;
}

bool composeModelQuake3Skin(const MaterialDefinition& definition, const MaterialImageSet& images, const ModelGamePreviewOptions& options,
	ModelGameSkin* out, QString* error)
{
	if (!out) {
		return false;
	}
	*out = {};
	const auto textureOf = [&](const MaterialStage& stage, int* frame) -> MaterialTexturePtr {
		*frame = -1;
		switch (stage.imageKind) {
		case MaterialImageKind::File:
			return images.find(stage.imagePath);
		case MaterialImageKind::White:
			return materialBuiltinTexture(QStringLiteral("$whiteimage"));
		case MaterialImageKind::Animation: {
			if (stage.animationFrames.isEmpty()) {
				return {};
			}
			// tr_shade.c R_BindAnimatedImage: whole frames of the speed.
			const qint64 index = qint64(std::floor(options.time * stage.animationFrequency));
			const int count = int(stage.animationFrames.size());
			*frame = int(((index % count) + count) % count);
			return images.find(stage.animationFrames.at(*frame));
		}
		default:
			return {};
		}
	};
	// The output takes the first usable stage image's size.
	QSize size(256, 256);
	for (const MaterialStage& stage : definition.stages) {
		int frame = -1;
		const MaterialTexturePtr texture = textureOf(stage, &frame);
		if (texture && texture->usable() && stage.imageKind != MaterialImageKind::White) {
			size = QSize(texture->width, texture->height);
			break;
		}
	}
	const int width = std::clamp(size.width(), 1, 2048), height = std::clamp(size.height(), 1, 2048);
	const qsizetype pixels = qsizetype(width) * height;
	QVector<Rgba> lit(pixels), unlit(pixels);
	QVector<double> coverage(pixels, 0.0);
	QStringList notes;
	const auto note = [&notes](const QString& text) {
		if (!notes.contains(text)) {
			notes << text;
		}
	};
	int drawn = 0;
	for (const MaterialStage& stage : definition.stages) {
		if (stage.blend.writesNothing() || stage.rgbGen.source == MaterialColorSource::Skip) {
			continue;
		}
		if (stage.imageKind == MaterialImageKind::Lightmap) {
			note(QCoreApplication::translate("VibeStudioModelGamePreview", "Models have no lightmap; $lightmap stages are left out."));
			continue;
		}
		int frame = -1;
		const MaterialTexturePtr texture = textureOf(stage, &frame);
		if (!texture || !texture->usable()) {
			note(QCoreApplication::translate("VibeStudioModelGamePreview", "A stage image was not found: %1").arg(stage.imagePath));
			continue;
		}
		if (frame >= 0) {
			out->frame = frame;
		}
		if (stage.tcGen.source == MaterialTexCoordSource::Environment) {
			note(QCoreApplication::translate("VibeStudioModelGamePreview",
				"tcGen environment depends on the view; the preview maps it like the base coordinates."));
		} else if (stage.tcGen.source != MaterialTexCoordSource::Base) {
			note(QCoreApplication::translate("VibeStudioModelGamePreview", "A stage's tcGen is drawn with the base coordinates."));
		}
		// Light-grid stages follow the viewport's shade; the rest are unlit.
		bool shaded = false;
		double gray[3] = {1.0, 1.0, 1.0};
		switch (stage.rgbGen.source) {
		case MaterialColorSource::LightingDiffuse:
		case MaterialColorSource::Vertex:
		case MaterialColorSource::ExactVertex:
		case MaterialColorSource::OneMinusVertex:
			shaded = true;
			break;
		case MaterialColorSource::Constant:
			gray[0] = stage.rgbGen.constant[0];
			gray[1] = stage.rgbGen.constant[1];
			gray[2] = stage.rgbGen.constant[2];
			break;
		case MaterialColorSource::Wave:
			gray[0] = gray[1] = gray[2] = evaluateQuake3WaveClamped(stage.rgbGen.wave, options.time);
			break;
		case MaterialColorSource::OneMinusEntity:
			gray[0] = gray[1] = gray[2] = 0.0;
			break;
		case MaterialColorSource::LightingSpecular:
			note(QCoreApplication::translate("VibeStudioModelGamePreview", "Specular lighting is not simulated."));
			break;
		default:
			break;
		}
		double alphaScale = 1.0;
		switch (stage.alphaGen.source) {
		case MaterialColorSource::Constant:
			alphaScale = stage.alphaGen.constantAlpha;
			break;
		case MaterialColorSource::Wave:
			alphaScale = evaluateQuake3WaveClamped(stage.alphaGen.wave, options.time);
			break;
		default:
			break;
		}
		const bool replace = stage.blend.isOpaqueReplace();
		++drawn;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				double s = (x + 0.5) / width, t = (y + 0.5) / height;
				applyTexMods(stage.tcMods, options.time, &s, &t);
				const Rgba texel = sampleTexture(*texture, s, t, stage.clamp, stage.nearest);
				const double alpha = texel.a * alphaScale;
				const bool pass = stage.alphaTest == MaterialAlphaTest::None || (stage.alphaTest == MaterialAlphaTest::Greater0 && alpha > 0.0)
					|| (stage.alphaTest == MaterialAlphaTest::Less128 && alpha < 0.5)
					|| (stage.alphaTest == MaterialAlphaTest::GreaterEqual128 && alpha >= 0.5);
				if (!pass) {
					continue;
				}
				const qsizetype at = qsizetype(y) * width + x;
				Rgba& l = lit[at];
				Rgba& u = unlit[at];
				const double src[3] = {texel.r * gray[0], texel.g * gray[1], texel.b * gray[2]};
				double* dl[3] = {&l.r, &l.g, &l.b};
				double* du[3] = {&u.r, &u.g, &u.b};
				const double destinationAlpha = coverage.at(at);
				for (int c = 0; c < 3; ++c) {
					const double destination = std::min(1.0, *dl[c] + *du[c]);
					const double sf = factor(stage.blend.source, src[c], alpha, destination, destinationAlpha);
					const double df = factor(stage.blend.destination, src[c], alpha, destination, destinationAlpha);
					*dl[c] = (shaded ? src[c] * sf : 0.0) + *dl[c] * df;
					*du[c] = (shaded ? 0.0 : src[c] * sf) + *du[c] * df;
				}
				// How much of what is behind the model the stage covers: an
				// opaque stage all of it, a blend its alpha, an add none.
				double covered = 0.0;
				if (replace || stage.alphaTest != MaterialAlphaTest::None) {
					covered = 1.0;
				} else if (stage.blend.destination == MaterialBlendFactor::OneMinusSourceAlpha) {
					covered = alpha;
				} else if (stage.blend.destination == MaterialBlendFactor::Zero) {
					covered = 1.0;
				}
				coverage[at] = destinationAlpha + (1.0 - destinationAlpha) * covered;
			}
		}
	}
	if (!definition.deforms.isEmpty()) {
		notes << QCoreApplication::translate("VibeStudioModelGamePreview", "deformVertexes changes the geometry in game and is not drawn here.");
	}
	out->base = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	QImage glow(width, height, QImage::Format_ARGB32_Premultiplied);
	bool anyGlow = false;
	for (int y = 0; y < height; ++y) {
		auto* base = reinterpret_cast<QRgb*>(out->base.scanLine(y));
		auto* added = reinterpret_cast<QRgb*>(glow.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const qsizetype at = qsizetype(y) * width + x;
			const Rgba& l = lit.at(at);
			const Rgba& u = unlit.at(at);
			double cover = std::clamp(coverage.at(at), 0.0, 1.0);
			const double glowPeak = std::max({u.r, u.g, u.b});
			// An additive-only texel shows as translucent light.
			if (cover < 1.0 / 255.0 && glowPeak > 0.0) {
				cover = std::min(1.0, glowPeak);
			}
			const int a = int(std::lround(cover * 255.0));
			const auto channel = [a](double value) { return std::clamp(int(std::lround(value * 255.0)), 0, a); };
			base[x] = qRgba(channel(l.r), channel(l.g), channel(l.b), a);
			const auto glowChannel = [](double value) { return std::clamp(int(std::lround(value * 255.0)), 0, 255); };
			added[x] = qRgba(glowChannel(u.r), glowChannel(u.g), glowChannel(u.b), 255);
			anyGlow = anyGlow || glowPeak > 0.0;
		}
	}
	if (anyGlow) {
		out->glow = glow;
	}
	out->stagesDrawn = drawn;
	out->notes = notes;
	if (drawn == 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelGamePreview", "The shader %1 has no stage that can be drawn on a model.").arg(definition.name);
		}
		return false;
	}
	return true;
}

bool composeModelGameSkin(const ModelMesh& mesh, int surface, const MaterialDefinition* definition, const MaterialImageSet* images,
	const QImage& fallback, const ModelGamePreviewOptions& options, ModelGameSkin* out, QString* error)
{
	if (!out) {
		return false;
	}
	if (mesh.mdl.enabled && !mesh.embeddedSkins.isEmpty()) {
		const ModelEmbeddedSkin* chosen = modelEmbeddedSkinForSurface(mesh, surface);
		const int skin = chosen ? int(chosen - mesh.embeddedSkins.constData()) : 0;
		return composeModelQuakeSkin(mesh, skin, options, out, error);
	}
	if (definition && images && definition->engine == MaterialEngine::Quake3) {
		return composeModelQuake3Skin(*definition, *images, options, out, error);
	}
	*out = {};
	out->base = fallback;
	if (definition && definition->engine == MaterialEngine::Doom3) {
		out->notes << QCoreApplication::translate("VibeStudioModelGamePreview",
			"Doom 3 bump and specular interaction is not simulated; the diffuse image is shown.");
	}
	return !fallback.isNull();
}

} // namespace vibestudio
