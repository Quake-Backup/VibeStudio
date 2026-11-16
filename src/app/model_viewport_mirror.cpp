// Mirror panes: a viewport that shows what another viewport shows (mesh,
// frame, shading, skins, selection and overlays) through its own camera. The
// modeller's four-view layout keeps one interactive viewport and three
// mirrors, and swaps a mirror in when the user clicks it.
#include "app/model_viewport.h"

namespace vibestudio {

void ModelViewport::setViewLabel(const QString& label)
{
	if (label == m_viewLabel) {
		return;
	}
	m_viewLabel = label;
	update();
}

void ModelViewport::mirrorDisplayFrom(const ModelViewport& source)
{
	if (&source == this) {
		return;
	}
	// Mesh copies share their arrays until either side changes, so equal
	// buffers mean an unchanged mesh and the costly rebuild can be skipped.
	const bool meshChanged = m_hasMesh != source.m_hasMesh || m_mesh.surfaces.constData() != source.m_mesh.surfaces.constData()
		|| m_mesh.frames.constData() != source.m_mesh.frames.constData() || m_mesh.surfaces.size() != source.m_mesh.surfaces.size()
		|| m_mesh.skeleton.joints.constData() != source.m_mesh.skeleton.joints.constData()
		|| m_mesh.skeleton.clips.constData() != source.m_mesh.skeleton.clips.constData() || m_mesh.tags.constData() != source.m_mesh.tags.constData()
		|| m_mesh.collisionBoxes.constData() != source.m_mesh.collisionBoxes.constData();
	const bool hadMesh = m_hasMesh;
	if (meshChanged) {
		m_mesh = source.m_mesh;
		m_texCoordBackup.clear();
		++m_meshRevision;
		m_hasMesh = source.m_hasMesh;
		m_frameTotal = source.m_frameTotal;
		m_editPivotCache = {};
		rebuildMeshTriangles();
	}
	// The source finishes a frame for every camera move of its own, and asks
	// its mirrors to follow each time; a mirror whose copy of the display is
	// unchanged keeps its frame rather than drawing it again.
	bool changed = meshChanged;
	const auto take = [&changed](auto& target, const auto& value) {
		if (!(target == value)) {
			target = value;
			changed = true;
		}
	};
	// Images compare by identity: equal pixels in another image still upload anew.
	const auto takeImage = [&changed](QImage& target, const QImage& value) {
		if (target.cacheKey() != value.cacheKey()) {
			target = value;
			changed = true;
		}
	};
	const auto takeImages = [&changed](QHash<int, QImage>& target, const QHash<int, QImage>& value) {
		bool same = target.size() == value.size();
		for (auto it = value.cbegin(); same && it != value.cend(); ++it) {
			const auto found = target.constFind(it.key());
			same = found != target.cend() && found->cacheKey() == it->cacheKey();
		}
		if (!same) {
			target = value;
			changed = true;
		}
	};
	takeImage(m_skin, source.m_skin);
	take(m_skinHasAlpha, source.m_skinHasAlpha);
	takeImages(m_surfaceSkins, source.m_surfaceSkins);
	take(m_surfaceSkinAlpha, source.m_surfaceSkinAlpha);
	takeImages(m_gameSkins, source.m_gameSkins);
	takeImages(m_gameGlows, source.m_gameGlows);
	take(m_gameSkinAlpha, source.m_gameSkinAlpha);
	take(m_gameNearest, source.m_gameNearest);
	m_surfaceSkinSources = source.m_surfaceSkinSources;
	m_gameSkinSources = source.m_gameSkinSources;
	m_gameGlowSources = source.m_gameGlowSources;
	take(m_renderMode, source.m_renderMode);
	take(m_backfaceCulling, source.m_backfaceCulling);
	take(m_showGrid, source.m_showGrid);
	take(m_showAxes, source.m_showAxes);
	take(m_showEdges, source.m_showEdges);
	take(m_polygonEdges, source.m_polygonEdges);
	take(m_faceShading, source.m_faceShading);
	take(m_fog, source.m_fog);
	take(m_fogStart, source.m_fogStart);
	take(m_fogEnd, source.m_fogEnd);
	take(m_fogStrength, source.m_fogStrength);
	take(m_faceGrid, source.m_faceGrid);
	if (m_faceGridTriangles != source.m_faceGridTriangles) {
		m_faceGridTriangles = source.m_faceGridTriangles;
		++m_faceGridRevision;
		changed = true;
	}
	take(m_highContrast, source.m_highContrast);
	take(m_reducedMotion, source.m_reducedMotion);
	take(m_frame, source.m_frame);
	take(m_rangeFirst, source.m_rangeFirst);
	take(m_rangeCount, source.m_rangeCount);
	take(m_animation, source.m_animation);
	take(m_animationIndex, source.m_animationIndex);
	take(m_fps, source.m_fps);
	take(m_interpolateAnimation, source.m_interpolateAnimation);
	take(m_frameBlend, source.m_frameBlend);
	take(m_blendFrame, source.m_blendFrame);
	take(m_nativeMdl, source.m_nativeMdl);
	take(m_mdlSkinVisible, source.m_mdlSkinVisible);
	const auto& playback = source.m_mdlPlayback;
	if (m_mdlPlayback.nativeFrame != playback.nativeFrame || m_mdlPlayback.skin != playback.skin || m_mdlPlayback.timing != playback.timing
		|| m_mdlPlayback.seconds != playback.seconds || m_mdlPlayback.syncPhase != playback.syncPhase) {
		m_mdlPlayback = playback;
		changed = true;
	}
	const auto& sample = source.m_mdlSample;
	if (m_mdlSample.frame != sample.frame || m_mdlSample.skinMember != sample.skinMember || m_mdlSample.frameCycle != sample.frameCycle
		|| m_mdlSample.skinCycle != sample.skinCycle) {
		m_mdlSample = sample;
		changed = true;
	}
	bool sameMdlSkins = m_mdlPlaybackSkins.size() == source.m_mdlPlaybackSkins.size();
	for (qsizetype index = 0; sameMdlSkins && index < m_mdlPlaybackSkins.size(); ++index) {
		sameMdlSkins = m_mdlPlaybackSkins.at(index).cacheKey() == source.m_mdlPlaybackSkins.at(index).cacheKey();
	}
	if (!sameMdlSkins) {
		m_mdlPlaybackSkins = source.m_mdlPlaybackSkins;
		changed = true;
	}
	take(m_highlighted, source.m_highlighted);
	take(m_highlightCount, source.m_highlightCount);
	take(m_edgeSelectionSurface, source.m_edgeSelectionSurface);
	take(m_selectedEdges, source.m_selectedEdges);
	take(m_editSurface, source.m_editSurface);
	take(m_editTag, source.m_editTag);
	take(m_editJoint, source.m_editJoint);
	take(m_showJoints, source.m_showJoints);
	take(m_jointPicking, source.m_jointPicking);
	take(m_xrayJoints, source.m_xrayJoints);
	take(m_jointDescendants, source.m_jointDescendants);
	take(m_showTags, source.m_showTags);
	take(m_editVertices, source.m_editVertices);
	take(m_editSurfaces, source.m_editSurfaces);
	take(m_xrayVertices, source.m_xrayVertices);
	take(m_vertexPicking, source.m_vertexPicking);
	take(m_edgePicking, source.m_edgePicking);
	take(m_tagPicking, source.m_tagPicking);
	take(m_showCollision, source.m_showCollision);
	take(m_collisionPicking, source.m_collisionPicking);
	take(m_editCollision, source.m_editCollision);
	if (meshChanged && !hadMesh && m_hasMesh) {
		m_center = source.m_center;
		m_radius = source.m_radius;
		frameModel();
	}
	// Framing a newly opened mirror cancels local gestures. Copy the shared
	// joint preview afterwards so its first image has the same pose.
	const bool jointPreview = (source.m_editMoveActive || source.m_mirroredJointPreview) && !source.m_editJoint.isEmpty();
	const ModelTransform transform = jointPreview ? source.m_editTransform : ModelTransform{};
	const auto sameVector = [](const ModelVec3& a, const ModelVec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
	bool sameTransform = sameVector(m_editTransform.translation, transform.translation) && sameVector(m_editTransform.rotation, transform.rotation)
		&& sameVector(m_editTransform.scale, transform.scale) && sameVector(m_editTransform.pivot, transform.pivot)
		&& m_editTransform.basis.world == transform.basis.world;
	for (size_t axis = 0; sameTransform && axis < 3; ++axis) {
		sameTransform = sameVector(m_editTransform.basis.axes[axis], transform.basis.axes[axis]);
	}
	if (m_mirroredJointPreview != jointPreview || !sameTransform) {
		m_mirroredJointPreview = jointPreview;
		m_editTransform = transform;
		changed = true;
	}
	if (!changed) {
		return;
	}
	invalidateProjection();
	// Keep the last image until the new one is ready, so syncing never flickers.
	invalidateRaster(false);
	update();
}

} // namespace vibestudio
