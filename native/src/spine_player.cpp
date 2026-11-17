#include "spine_player.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "image.h"
#include "json.h"
#include "log.h"

namespace {

class DirectTextureLoader : public spine::TextureLoader {
 public:
  explicit DirectTextureLoader(Renderer& renderer) : renderer_(renderer) {}

  void load(spine::AtlasPage& page, const spine::String& path) override {
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    if (!loadImageRGBA(widen(path.buffer()), rgba, width, height)) {
      PETLOG("pet: failed to load texture %s\n", path.buffer());
      return;
    }
    Texture2D* texture = renderer_.createTexture(rgba.data(), width, height);
    PETLOG("pet: texture %s %dx%d -> %p\n", path.buffer(), width, height, static_cast<void*>(texture));
    if (texture) page.setRendererObject(texture);
  }

  void unload(void*) override {}

 private:
  Renderer& renderer_;
};

struct Bounds {
  float minX = FLT_MAX;
  float minY = FLT_MAX;
  float maxX = -FLT_MAX;
  float maxY = -FLT_MAX;
  bool valid() const { return maxX >= minX && maxY >= minY; }
};

Bounds computeBounds(spine::Skeleton& skeleton, const char* nameFilter = nullptr) {
  Bounds bounds;
  size_t capacity = 0;
  std::vector<float> vertices;
  auto& drawOrder = skeleton.getDrawOrder();
  for (size_t i = 0; i < drawOrder.size(); ++i) {
    spine::Slot* slot = drawOrder[i];
    if (!slot->getBone().isActive()) continue;
    if (nameFilter && std::string(slot->getData().getName().buffer()).find(nameFilter) ==
                          std::string::npos) {
      continue;
    }
    spine::Attachment* attachment = slot->getAttachment();
    if (!attachment) continue;
    size_t length = 0;
    if (attachment->getRTTI().instanceOf(spine::RegionAttachment::rtti)) {
      length = 8;
      if (capacity < length) {
        capacity = length;
        vertices.resize(capacity);
      }
      auto* region = static_cast<spine::RegionAttachment*>(attachment);
      region->computeWorldVertices(slot->getBone(), vertices.data(), 0, 2);
    } else if (attachment->getRTTI().instanceOf(spine::MeshAttachment::rtti)) {
      auto* mesh = static_cast<spine::MeshAttachment*>(attachment);
      length = mesh->getWorldVerticesLength();
      if (capacity < length) {
        capacity = length;
        vertices.resize(capacity);
      }
      mesh->computeWorldVertices(*slot, 0, length, vertices.data(), 0, 2);
    } else {
      continue;
    }
    for (size_t v = 0; v + 1 < length; v += 2) {
      bounds.minX = std::min(bounds.minX, vertices[v]);
      bounds.minY = std::min(bounds.minY, vertices[v + 1]);
      bounds.maxX = std::max(bounds.maxX, vertices[v]);
      bounds.maxY = std::max(bounds.maxY, vertices[v + 1]);
    }
  }
  return bounds;
}

const char* const kIdleKeys[2][4] = {
    {"standIdle", "sitIdle", "lieIdle", nullptr},
    {"idle", "skill1Idle", "skill2Idle", "skill3Idle"},
};

const char* fallbackAnchorKey(const std::string& anim) {
  if (anim == "move" || anim == "interact") return "standIdle";
  if (anim == "attack" || anim == "start" || anim == "die") return "idle";
  if (anim == "start2") return "skill1Idle";
  if (anim == "skill1Die" || anim == "skill1End" || anim == "skill1Loop") return "skill1Idle";
  if (anim == "start2Begin" || anim == "skill2Begin" || anim == "skill2TakeoffBegin" ||
      anim == "skill2TakeoffEnd" || anim == "skill2TakeoffLoop" || anim == "skill2Loop" ||
      anim == "skill2End" || anim == "skill2Die") {
    return "skill2Idle";
  }
  if (anim == "start3Begin" || anim == "restart3" || anim == "skill3Die" || anim == "skill3End" ||
      anim == "skill3Loop" || anim == "skill3Combat" || anim == "skill3Move") {
    return "skill3Idle";
  }
  return nullptr;
}

spine::BlendMode blendForSlot(spine::Slot* slot) {
  return slot->getData().getBlendMode();
}

}  // namespace

bool loadOperatorManifest(const std::wstring& assetRoot, const std::string& id,
                          OperatorManifest& out) {
  out = OperatorManifest();
  out.id = id;
  std::wstring directory = assetRoot + L"\\operators\\" + widen(id);
  std::string text;
  if (!readFileText(directory + L"\\pet.json", text)) return false;
  mini::Json root;
  if (!mini::parseJson(text, root)) return false;
  out.directory = directory;
  out.name = root.find("name") ? root.find("name")->asText(id) : id;
  out.rootBone = root.find("rootBone") ? root.find("rootBone")->asText("F_All") : "F_All";
  out.rootClamp = static_cast<float>(root.find("rootClamp") ? root.find("rootClamp")->asNumber(80) : 80);
  out.referenceHeight = static_cast<float>(
      root.find("referenceHeight") ? root.find("referenceHeight")->asNumber(520) : 520);
  out.referenceWidth = static_cast<float>(
      root.find("referenceWidth") ? root.find("referenceWidth")->asNumber(600) : 600);
  if (const mini::Json* avatar = root.find("avatar")) {
    out.avatarPath = directory + L"\\" + widen(avatar->asText());
  }
  const mini::Json* models = root.find("models");
  const mini::Json* animations = root.find("animations");
  if (!models || !animations) return false;
  const char* names[2] = {"base", "battle"};
  for (int i = 0; i < 2; ++i) {
    const mini::Json* model = models->find(names[i]);
    if (!model) return false;
    const mini::Json* skel = model->find("skel");
    const mini::Json* atlas = model->find("atlas");
    if (!skel || !atlas) return false;
    out.skelPath[i] = directory + L"\\" + widen(skel->asText());
    out.atlasPath[i] = directory + L"\\" + widen(atlas->asText());
    const mini::Json* anims = animations->find(names[i]);
    if (!anims || anims->type != mini::Json::Type::Object) return false;
    for (const auto& entry : anims->fields) {
      if (entry.second.type == mini::Json::Type::String) {
        out.animations[i][entry.first] = entry.second.text;
      }
    }
  }
  out.valid = true;
  return true;
}

SpinePlayer::~SpinePlayer() { shutdown(); }

void SpinePlayer::shutdown() {
  activeEntry_ = nullptr;
  if (state_) {
    delete state_;
    state_ = nullptr;
  }
  if (stateData_) {
    delete stateData_;
    stateData_ = nullptr;
  }
  if (skeleton_) {
    delete skeleton_;
    skeleton_ = nullptr;
  }
  for (Model& model : models_) {
    if (model.data) {
      delete model.data;
      model.data = nullptr;
    }
    if (model.atlas) {
      delete model.atlas;
      model.atlas = nullptr;
    }
    model.texture = nullptr;
  }
  ready_ = false;
}

bool SpinePlayer::loadModel(Renderer& renderer, const OperatorManifest& manifest, int formIndex,
                            Model& out) {
  std::string atlasText;
  if (!readFileText(manifest.atlasPath[formIndex], atlasText)) {
    PETLOG("pet: cannot read atlas %ls\n", manifest.atlasPath[formIndex].c_str());
    return false;
  }
  auto* loader = new DirectTextureLoader(renderer);
  std::wstring atlasDirectory = manifest.atlasPath[formIndex];
  size_t slash = atlasDirectory.find_last_of(L"\\/");
  if (slash != std::wstring::npos) atlasDirectory.resize(slash);
  std::string directory = narrow(atlasDirectory);
  out.atlas = new spine::Atlas(atlasText.c_str(), static_cast<int>(atlasText.size()),
                               directory.c_str(), loader, true);
  out.texture = nullptr;
  auto& pages = out.atlas->getPages();
  if (!pages.size()) {
    PETLOG("pet: atlas has no pages\n");
    return false;
  }
  out.texture = static_cast<Texture2D*>(pages[0]->getRendererObject());
  if (!out.texture) {
    PETLOG("pet: atlas page has no texture\n");
    return false;
  }

  std::vector<unsigned char> binary;
  if (!readFileBytes(manifest.skelPath[formIndex], binary)) {
    PETLOG("pet: cannot read skeleton %ls\n", manifest.skelPath[formIndex].c_str());
    return false;
  }
  spine::SkeletonBinary reader(new spine::AtlasAttachmentLoader(out.atlas), true);
  out.data = reader.readSkeletonData(binary.data(), static_cast<int>(binary.size()));
  if (!out.data) {
    PETLOG("pet: skeleton parse failed: %s\n", reader.getError().buffer());
    return false;
  }
  {
    int counts[4] = {0, 0, 0, 0};
    auto& slots = out.data->getSlots();
    for (size_t i = 0; i < slots.size(); ++i) {
      int mode = static_cast<int>(slots[i]->getBlendMode());
      if (mode >= 0 && mode < 4) counts[mode]++;
    }
    PETLOG("pet: model %d slots=%zu blend[normal=%d add=%d mul=%d screen=%d]\n", formIndex,
           slots.size(), counts[0], counts[1], counts[2], counts[3]);
  }

  for (int i = 0; i < 4; ++i) {
    const char* key = kIdleKeys[formIndex][i];
    if (!key) break;
    auto it = manifest.animations[formIndex].find(key);
    if (it == manifest.animations[formIndex].end()) continue;
    const char* animation = out.data->findAnimation(it->second.c_str()) ? it->second.c_str() : nullptr;
    if (!animation) continue;
    spine::Skeleton skeleton(out.data);
    spine::AnimationStateData stateData(out.data);
    spine::AnimationState state(&stateData);
    state.setAnimation(0, animation, false);
    state.update(0.3f);
    state.apply(skeleton);
    skeleton.updateWorldTransform();
    Bounds bounds = computeBounds(skeleton);
    if (bounds.valid()) {
      Anchor anchor;
      anchor.x = bounds.minX + (bounds.maxX - bounds.minX) * 0.5f;
      anchor.y = bounds.minY;
      out.anchors[key] = anchor;
      Bounds head = computeBounds(skeleton, "Head");
      if (!head.valid()) head = bounds;
      Anchor topRight;
      topRight.x = head.maxX - 2.0f;
      topRight.y = head.maxY - 2.0f;
      out.boxesTopRight[key] = topRight;
      Anchor center;
      center.x = (head.minX + head.maxX) * 0.5f;
      center.y = head.maxY;
      out.headCenters[key] = center;
    }
  }
  return true;
}

bool SpinePlayer::load(Renderer& renderer, const OperatorManifest& manifest, Form initialForm) {
  manifest_ = &manifest;
  if (!loadModel(renderer, manifest, 0, models_[0])) return false;
  if (!loadModel(renderer, manifest, 1, models_[1])) return false;
  listener_.player = this;
  form_ = initialForm;
  setModel(initialForm);
  ready_ = true;
  return true;
}

void SpinePlayer::setViewport(int widthPixels, int heightPixels, float dpiScale) {
  viewportWidth_ = std::max(1, widthPixels);
  viewportHeight_ = std::max(1, heightPixels);
  bottomMargin_ = 8.0f * dpiScale;
  zoom_ = (manifest_->referenceHeight * 1.08f) / static_cast<float>(viewportHeight_);
}

void SpinePlayer::setModel(Form form) {
  form_ = form;
  activeEntry_ = nullptr;
  const Model& model = models_[form == Form::Base ? 0 : 1];
  if (!model.data) return;
  if (skeleton_) delete skeleton_;
  skeleton_ = new spine::Skeleton(model.data);
  skeleton_->setToSetupPose();
  if (stateData_) delete stateData_;
  stateData_ = new spine::AnimationStateData(model.data);
  stateData_->setDefaultMix(0.12f);
  if (state_) delete state_;
  state_ = new spine::AnimationState(stateData_);
  state_->setListener(&listener_);
}

SpinePlayer::Anchor SpinePlayer::anchorFor(const Step& step) const {
  const Model& model = models_[step.model == Form::Base ? 0 : 1];
  auto it = model.anchors.find(step.anim);
  if (it != model.anchors.end()) return it->second;
  const char* fallback = fallbackAnchorKey(step.anim);
  if (fallback) {
    auto fallbackIt = model.anchors.find(fallback);
    if (fallbackIt != model.anchors.end()) return fallbackIt->second;
  }
  return Anchor{};
}

void SpinePlayer::play(const PetPlan& plan) {
  if (!ready_ || !manifest_) return;
  PetPlan filtered = plan;
  filtered.steps.clear();
  for (const Step& step : plan.steps) {
    const auto& animations = manifest_->animations[step.model == Form::Base ? 0 : 1];
    if (animations.find(step.anim) != animations.end()) filtered.steps.push_back(step);
  }
  if (filtered.steps.empty()) return;
  plan_ = filtered;
  stepIndex_ = 0;
  anchorBase_ = anchorFor(plan_.steps.back());
  targetAnchor_.x = anchorBase_.x * static_cast<float>(facing_);
  targetAnchor_.y = anchorBase_.y;
  playStep(0);
}

void SpinePlayer::playStep(int index) {
  if (!state_ || !manifest_) return;
  const Step& step = plan_.steps[static_cast<size_t>(index)];
  if (step.model != form_) setModel(step.model);
  const auto& animations = manifest_->animations[step.model == Form::Base ? 0 : 1];
  auto it = animations.find(step.anim);
  if (it == animations.end()) return;
  spine::TrackEntry* entry = state_->setAnimation(0, it->second.c_str(), step.loop);
  bool isLast = index == static_cast<int>(plan_.steps.size()) - 1;
  if (step.loop) {
    activeEntry_ = nullptr;
    if (isLast && onIdle_) onIdle_();
  } else {
    activeEntry_ = entry;
  }
}

void SpinePlayer::advance() {
  int next = stepIndex_ + 1;
  if (next >= static_cast<int>(plan_.steps.size())) {
    activeEntry_ = nullptr;
    if (onIdle_) onIdle_();
    return;
  }
  stepIndex_ = next;
  playStep(next);
}

void SpinePlayer::onComplete(spine::AnimationState* state, spine::TrackEntry* entry) {
  if (state == state_ && entry == activeEntry_ && entry) advance();
}

void SpinePlayer::Listener::callback(spine::AnimationState* state, spine::EventType type,
                                     spine::TrackEntry* entry, spine::Event*) {
  if (player && type == spine::EventType_Complete) player->onComplete(state, entry);
}

void SpinePlayer::update(float deltaSeconds) {
  if (!ready_ || !skeleton_ || !state_) return;
  state_->update(std::min(deltaSeconds, 0.05f));
  state_->apply(*skeleton_);
  spine::Bone* bone = skeleton_->findBone(manifest_->rootBone.c_str());
  if (bone) {
    float clamp = manifest_->rootClamp;
    float baseX = bone->getData().getX();
    float baseY = bone->getData().getY();
    bone->setX(std::max(baseX - clamp, std::min(baseX + clamp, bone->getX())));
    bone->setY(std::max(baseY - clamp, std::min(baseY + clamp, bone->getY())));
  }
  skeleton_->setScaleX(static_cast<float>(facing_));
  skeleton_->updateWorldTransform();
  float lerp = std::min(1.0f, deltaSeconds * 9.0f);
  cameraAnchor_.x += (targetAnchor_.x - cameraAnchor_.x) * lerp;
  cameraAnchor_.y += (targetAnchor_.y - cameraAnchor_.y) * lerp;
}

void SpinePlayer::render(Renderer& renderer) {
  if (!ready_ || !skeleton_ || !manifest_) return;
  const float width = static_cast<float>(viewportWidth_);
  const float height = static_cast<float>(viewportHeight_);
  const float cameraX = cameraAnchor_.x;
  const float cameraY = cameraAnchor_.y + zoom_ * height * 0.5f - zoom_ * bottomMargin_;
  screenLeft_ = FLT_MAX;
  screenTop_ = FLT_MAX;
  screenRight_ = -FLT_MAX;
  screenBottom_ = -FLT_MAX;

  auto toScreenX = [&](float worldX) { return width * 0.5f + (worldX - cameraX) / zoom_; };
  auto toScreenY = [&](float worldY) { return height * 0.5f - (worldY - cameraY) / zoom_; };

  struct ClipVertex {
    float x, y, u, v;
  };

  std::vector<float> worldVertices;
  std::vector<float> uvPairs;
  std::vector<unsigned short> triangleList;
  std::vector<Vertex> vertices;
  std::vector<unsigned short> clippedIndices;
  std::vector<ClipVertex> polygonIn;
  std::vector<ClipVertex> polygonOut;

  const Model& model = models_[form_ == Form::Base ? 0 : 1];
  if (!model.texture) return;

  hitTriangles_.clear();
  auto recordTriangles = [&](const std::vector<Vertex>& verts, const unsigned short* indices,
                             size_t count) {
    for (size_t i = 0; i + 2 < count; i += 3) {
      const Vertex& a = verts[indices[i]];
      const Vertex& b = verts[indices[i + 1]];
      const Vertex& c = verts[indices[i + 2]];
      hitTriangles_.push_back(a.x);
      hitTriangles_.push_back(a.y);
      hitTriangles_.push_back(b.x);
      hitTriangles_.push_back(b.y);
      hitTriangles_.push_back(c.x);
      hitTriangles_.push_back(c.y);
    }
  };

  clipPolygon_.clear();
  clipEndSlot_ = nullptr;
  float clipOrientation = 1.0f;

  auto beginClip = [&](spine::Slot& clipSlot, spine::ClippingAttachment* clip) {
    size_t length = clip->getWorldVerticesLength();
    if (length < 6) return;
    clipPolygon_.resize(length);
    clip->computeWorldVertices(clipSlot, 0, length, clipPolygon_.data(), 0, 2);
    float area2 = 0.0f;
    size_t count = length / 2;
    for (size_t i = 0; i < count; ++i) {
      size_t j = (i + 1) % count;
      area2 += clipPolygon_[i * 2] * clipPolygon_[j * 2 + 1] -
               clipPolygon_[j * 2] * clipPolygon_[i * 2 + 1];
    }
    clipOrientation = area2 >= 0.0f ? 1.0f : -1.0f;
    clipEndSlot_ = clip->getEndSlot();
  };

  auto endClipIfNeeded = [&](spine::Slot& slot) {
    if (clipEndSlot_ && &slot.getData() == clipEndSlot_) {
      clipPolygon_.clear();
      clipEndSlot_ = nullptr;
    }
  };

  auto clipTriangle = [&](const ClipVertex& a, const ClipVertex& b, const ClipVertex& c) {
    polygonIn.clear();
    polygonIn.push_back(a);
    polygonIn.push_back(b);
    polygonIn.push_back(c);
    size_t count = clipPolygon_.size() / 2;
    for (size_t edge = 0; edge < count && !polygonIn.empty(); ++edge) {
      float ax = clipPolygon_[edge * 2];
      float ay = clipPolygon_[edge * 2 + 1];
      size_t nextEdge = (edge + 1) % count;
      float bx = clipPolygon_[nextEdge * 2];
      float by = clipPolygon_[nextEdge * 2 + 1];
      polygonOut.clear();
      size_t size = polygonIn.size();
      for (size_t i = 0; i < size; ++i) {
        const ClipVertex& current = polygonIn[i];
        const ClipVertex& previous = polygonIn[(i + size - 1) % size];
        float dc = ((bx - ax) * (current.y - ay) - (by - ay) * (current.x - ax)) * clipOrientation;
        float dp = ((bx - ax) * (previous.y - ay) - (by - ay) * (previous.x - ax)) * clipOrientation;
        bool currentInside = dc >= -0.0001f;
        bool previousInside = dp >= -0.0001f;
        if (currentInside) {
          if (!previousInside) {
            float t = dp / (dp - dc);
            polygonOut.push_back({previous.x + (current.x - previous.x) * t,
                                  previous.y + (current.y - previous.y) * t,
                                  previous.u + (current.u - previous.u) * t,
                                  previous.v + (current.v - previous.v) * t});
          }
          polygonOut.push_back(current);
        } else if (previousInside) {
          float t = dp / (dp - dc);
          polygonOut.push_back({previous.x + (current.x - previous.x) * t,
                                previous.y + (current.y - previous.y) * t,
                                previous.u + (current.u - previous.u) * t,
                                previous.v + (current.v - previous.v) * t});
        }
      }
      polygonIn.swap(polygonOut);
    }
    if (polygonIn.size() < 3) return;
    unsigned short base = static_cast<unsigned short>(vertices.size());
    for (const ClipVertex& vertex : polygonIn) {
      Vertex out;
      out.x = toScreenX(vertex.x);
      out.y = toScreenY(vertex.y);
      out.u = vertex.u;
      out.v = vertex.v;
      out.r = 0.0f;
      out.g = 0.0f;
      out.b = 0.0f;
      out.a = 0.0f;
      vertices.push_back(out);
      screenLeft_ = std::min(screenLeft_, out.x);
      screenRight_ = std::max(screenRight_, out.x);
      screenTop_ = std::min(screenTop_, out.y);
      screenBottom_ = std::max(screenBottom_, out.y);
    }
    for (size_t i = 1; i + 1 < polygonIn.size(); ++i) {
      clippedIndices.push_back(static_cast<unsigned short>(base));
      clippedIndices.push_back(static_cast<unsigned short>(base + i));
      clippedIndices.push_back(static_cast<unsigned short>(base + i + 1));
    }
  };

  auto& drawOrder = skeleton_->getDrawOrder();
  for (size_t step = 0; step < drawOrder.size(); ++step) {
    size_t index = reverseOrder_ ? drawOrder.size() - 1 - step : step;
    spine::Slot* slot = drawOrder[index];
    if (!slot->getBone().isActive()) {
      endClipIfNeeded(*slot);
      continue;
    }
    if (!slotFilter_.empty() &&
        std::string(slot->getData().getName().buffer()).find(slotFilter_) == std::string::npos) {
      endClipIfNeeded(*slot);
      continue;
    }
    spine::Attachment* attachment = slot->getAttachment();
    if (!attachment) {
      endClipIfNeeded(*slot);
      continue;
    }
    if (attachment->getRTTI().instanceOf(spine::ClippingAttachment::rtti)) {
      beginClip(*slot, static_cast<spine::ClippingAttachment*>(attachment));
      continue;
    }
    const spine::Color& slotColor = slot->getColor();
    if (slotColor.a > 0.0f) {
      float r = slotColor.r;
      float g = slotColor.g;
      float b = slotColor.b;
      float a = slotColor.a;
      bool renderable = true;
      if (attachment->getRTTI().instanceOf(spine::RegionAttachment::rtti)) {
        auto* region = static_cast<spine::RegionAttachment*>(attachment);
        worldVertices.resize(8);
        region->computeWorldVertices(slot->getBone(), worldVertices.data(), 0, 2);
        auto& uvs = region->getUVs();
        static const int uvOrder[4] = {6, 0, 2, 4};
        uvPairs.resize(8);
        for (int v = 0; v < 4; ++v) {
          uvPairs[v * 2] = uvs[uvOrder[v]];
          uvPairs[v * 2 + 1] = uvs[uvOrder[v] + 1];
        }
        triangleList.assign({0, 1, 2, 2, 3, 0});
        const spine::Color& regionColor = region->getColor();
        r *= regionColor.r;
        g *= regionColor.g;
        b *= regionColor.b;
        a *= regionColor.a;
      } else if (attachment->getRTTI().instanceOf(spine::MeshAttachment::rtti)) {
        auto* mesh = static_cast<spine::MeshAttachment*>(attachment);
        size_t length = mesh->getWorldVerticesLength();
        if (!length) {
          endClipIfNeeded(*slot);
          continue;
        }
        worldVertices.resize(length);
        mesh->computeWorldVertices(*slot, 0, length, worldVertices.data(), 0, 2);
        auto& uvs = mesh->getUVs();
        uvPairs.assign(uvs.buffer(), uvs.buffer() + uvs.size());
        auto& triangles = mesh->getTriangles();
        triangleList.assign(triangles.buffer(), triangles.buffer() + triangles.size());
        const spine::Color& meshColor = mesh->getColor();
        r *= meshColor.r;
        g *= meshColor.g;
        b *= meshColor.b;
        a *= meshColor.a;
      } else {
        renderable = false;
      }

      if (renderable) {
        Blend blend =
            blendForSlot(slot) == spine::BlendMode_Additive ? Blend::Additive : Blend::Normal;
        size_t vertexCount = worldVertices.size() / 2;
        vertices.clear();
        clippedIndices.clear();
        if (clipPolygon_.empty()) {
          vertices.resize(vertexCount);
          for (size_t v = 0; v < vertexCount; ++v) {
            vertices[v].x = toScreenX(worldVertices[v * 2]);
            vertices[v].y = toScreenY(worldVertices[v * 2 + 1]);
            vertices[v].u = uvPairs[v * 2];
            vertices[v].v = uvPairs[v * 2 + 1];
            vertices[v].r = r;
            vertices[v].g = g;
            vertices[v].b = b;
            vertices[v].a = a;
            screenLeft_ = std::min(screenLeft_, vertices[v].x);
            screenRight_ = std::max(screenRight_, vertices[v].x);
            screenTop_ = std::min(screenTop_, vertices[v].y);
            screenBottom_ = std::max(screenBottom_, vertices[v].y);
          }
          renderer.draw(model.texture, blend, vertices.data(), static_cast<int>(vertexCount),
                        triangleList.data(), static_cast<int>(triangleList.size()));
          recordTriangles(vertices, triangleList.data(), triangleList.size());
        } else {
          for (size_t t = 0; t + 2 < triangleList.size(); t += 3) {
            ClipVertex a{worldVertices[triangleList[t] * 2], worldVertices[triangleList[t] * 2 + 1],
                         uvPairs[triangleList[t] * 2], uvPairs[triangleList[t] * 2 + 1]};
            ClipVertex b{worldVertices[triangleList[t + 1] * 2],
                         worldVertices[triangleList[t + 1] * 2 + 1],
                         uvPairs[triangleList[t + 1] * 2], uvPairs[triangleList[t + 1] * 2 + 1]};
            ClipVertex c{worldVertices[triangleList[t + 2] * 2],
                         worldVertices[triangleList[t + 2] * 2 + 1],
                         uvPairs[triangleList[t + 2] * 2], uvPairs[triangleList[t + 2] * 2 + 1]};
            clipTriangle(a, b, c);
          }
          if (!vertices.empty() && !clippedIndices.empty()) {
            for (Vertex& vertex : vertices) {
              vertex.r = r;
              vertex.g = g;
              vertex.b = b;
              vertex.a = a;
            }
            renderer.draw(model.texture, blend, vertices.data(), static_cast<int>(vertices.size()),
                          clippedIndices.data(), static_cast<int>(clippedIndices.size()));
            recordTriangles(vertices, clippedIndices.data(), clippedIndices.size());
          }
        }
      }
    }
    endClipIfNeeded(*slot);
  }
  clipPolygon_.clear();
  clipEndSlot_ = nullptr;
}

void SpinePlayer::setFacing(int facing) {
  if (facing == facing_) return;
  facing_ = facing < 0 ? -1 : 1;
  targetAnchor_.x = anchorBase_.x * static_cast<float>(facing_);
}

bool SpinePlayer::hitTest(float clientX, float clientY) const {
  if (screenRight_ <= screenLeft_) return false;
  if (clientX < screenLeft_ - 6.0f || clientX > screenRight_ + 6.0f ||
      clientY < screenTop_ - 6.0f || clientY > screenBottom_ + 6.0f) {
    return false;
  }
  for (size_t i = 0; i + 5 < hitTriangles_.size(); i += 6) {
    float ax = hitTriangles_[i];
    float ay = hitTriangles_[i + 1];
    float bx = hitTriangles_[i + 2];
    float by = hitTriangles_[i + 3];
    float cx = hitTriangles_[i + 4];
    float cy = hitTriangles_[i + 5];
    float d1 = (bx - ax) * (clientY - ay) - (by - ay) * (clientX - ax);
    float d2 = (cx - bx) * (clientY - by) - (cy - by) * (clientX - bx);
    float d3 = (ax - cx) * (clientY - cy) - (ay - cy) * (clientX - cx);
    if ((d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f) || (d1 <= 0.0f && d2 <= 0.0f && d3 <= 0.0f)) {
      return true;
    }
  }
  return false;
}

void SpinePlayer::screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const {
  float cameraX = cameraAnchor_.x;
  float cameraY = cameraAnchor_.y + zoom_ * viewportHeight_ * 0.5f - zoom_ * bottomMargin_;
  worldX = cameraX + (screenX - viewportWidth_ * 0.5f) * zoom_;
  worldY = cameraY - (screenY - viewportHeight_ * 0.5f) * zoom_;
}

void SpinePlayer::worldToScreen(float worldX, float worldY, float& screenX, float& screenY) const {
  float cameraX = cameraAnchor_.x;
  float cameraY = cameraAnchor_.y + zoom_ * viewportHeight_ * 0.5f - zoom_ * bottomMargin_;
  screenX = viewportWidth_ * 0.5f + (worldX - cameraX) / zoom_;
  screenY = viewportHeight_ * 0.5f - (worldY - cameraY) / zoom_;
}

bool SpinePlayer::dotScreenPosition(float& screenX, float& screenY) const {
  if (!ready_ || !manifest_) return false;
  const Model& model = models_[form_ == Form::Base ? 0 : 1];
  std::string key;
  if (!plan_.steps.empty()) key = plan_.steps.back().anim;
  auto box = model.boxesTopRight.find(key);
  if (box == model.boxesTopRight.end()) {
    const char* fallback = form_ == Form::Base ? "standIdle" : "idle";
    box = model.boxesTopRight.find(fallback);
    if (box == model.boxesTopRight.end()) return false;
  }
  float worldX = box->second.x * static_cast<float>(facing_);
  float worldY = box->second.y;
  float cameraY = cameraAnchor_.y + zoom_ * viewportHeight_ * 0.5f - zoom_ * bottomMargin_;
  screenX = viewportWidth_ * 0.5f + (worldX - cameraAnchor_.x) / zoom_;
  screenY = viewportHeight_ * 0.5f - (worldY - cameraY) / zoom_;
  return true;
}

bool SpinePlayer::headScreenAnchor(float& centerX, float& topY) const {
  if (!ready_ || !manifest_) return false;
  const Model& model = models_[form_ == Form::Base ? 0 : 1];
  std::string key;
  if (!plan_.steps.empty()) key = plan_.steps.back().anim;
  auto entry = model.headCenters.find(key);
  if (entry == model.headCenters.end()) {
    const char* fallback = form_ == Form::Base ? "standIdle" : "idle";
    entry = model.headCenters.find(fallback);
    if (entry == model.headCenters.end()) return false;
  }
  float worldX = entry->second.x * static_cast<float>(facing_);
  float worldY = entry->second.y;
  float cameraY = cameraAnchor_.y + zoom_ * viewportHeight_ * 0.5f - zoom_ * bottomMargin_;
  centerX = viewportWidth_ * 0.5f + (worldX - cameraAnchor_.x) / zoom_;
  topY = viewportHeight_ * 0.5f - (worldY - cameraY) / zoom_;
  return true;
}

void SpinePlayer::dumpAnimation(const std::string& petKey) {
  if (!manifest_) return;
  for (int m = 0; m < 2; ++m) {
    auto it = manifest_->animations[m].find(petKey);
    if (it == manifest_->animations[m].end()) continue;
    const Model& model = models_[m];
    if (!model.data) continue;
    spine::Skeleton skeleton(model.data);
    spine::AnimationStateData stateData(model.data);
    spine::AnimationState state(&stateData);
    state.setAnimation(0, it->second.c_str(), true);
    printf("== %s model=%d animation=%s\n", petKey.c_str(), m, it->second.c_str());
    auto describe = [&]() {
      std::string out;
      auto& slots = skeleton.getSlots();
      auto& order = skeleton.getDrawOrder();
      std::map<spine::Slot*, int> orderIndex;
      for (size_t i = 0; i < order.size(); ++i) orderIndex[order[i]] = static_cast<int>(i);
      for (size_t i = 0; i < slots.size(); ++i) {
        std::string name = slots[i]->getData().getName().buffer();
        if (name.find("Eye") == std::string::npos && name.find("Brow") == std::string::npos) continue;
        spine::Attachment* attachment = slots[i]->getAttachment();
        std::string extra;
        if ((name == "F_L_Eyeball_A" || name == "F_L_C_Eye_B" || name == "F_L_U_Eye_A") && attachment) {
          if (attachment->getRTTI().instanceOf(spine::MeshAttachment::rtti)) {
            auto* mesh = static_cast<spine::MeshAttachment*>(attachment);
            size_t length = mesh->getWorldVerticesLength();
            std::vector<float> verts(length);
            mesh->computeWorldVertices(*slots[i], 0, length, verts.data(), 0, 2);
            float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
            for (size_t v = 0; v + 1 < length; v += 2) {
              minX = std::min(minX, verts[v]);
              maxX = std::max(maxX, verts[v]);
              minY = std::min(minY, verts[v + 1]);
              maxY = std::max(maxY, verts[v + 1]);
            }
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "[mesh n=%zu tri=%zu deform=%zu %.1fx%.1f] ", length / 2,
                     mesh->getTriangles().size(), slots[i]->getDeform().size(), maxX - minX,
                     maxY - minY);
            extra = tmp;
            if (name == "F_L_Eyeball_A" && slots[i]->getDeform().size()) {
              char more[128];
              snprintf(more, sizeof(more), "[d0=%.1f d1=%.1f d2=%.1f d3=%.1f] ",
                       slots[i]->getDeform()[0], slots[i]->getDeform()[1],
                       slots[i]->getDeform()[2], slots[i]->getDeform()[3]);
              extra += more;
            }
          } else if (attachment->getRTTI().instanceOf(spine::RegionAttachment::rtti)) {
            auto* region = static_cast<spine::RegionAttachment*>(attachment);
            float verts[8];
            region->computeWorldVertices(slots[i]->getBone(), verts, 0, 2);
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "[region %.1fx%.1f] ",
                     std::abs(verts[0] - verts[4]), std::abs(verts[1] - verts[5]));
            extra = tmp;
          }
        }
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%s=%s(a=%.2f,z=%d)%s ", name.c_str(),
                 attachment ? attachment->getName().buffer() : "-", slots[i]->getColor().a,
                 orderIndex[slots[i]], extra.c_str());
        out += buffer;
      }
      return out;
    };
    std::string last;
    std::string lastAll;
    for (int frame = 0; frame < 120; ++frame) {
      state.update(1.0f / 30.0f);
      state.apply(skeleton);
      skeleton.updateWorldTransform();
      {
        std::string all;
        auto& slots = skeleton.getSlots();
        for (size_t i = 0; i < slots.size(); ++i) {
          spine::Attachment* attachment = slots[i]->getAttachment();
          if (!attachment) continue;
          all += std::string(slots[i]->getData().getName().buffer()) + "=" +
                 attachment->getName().buffer() + " ";
        }
        if (all != lastAll) {
          printf("  ATTACH t=%.2f changed\n", frame / 30.0f);
          lastAll = all;
        }
      }
      std::string current = describe();
      if (current != last) {
        printf("  t=%.2f %s\n", frame / 30.0f, current.c_str());
        last = current;
      }
      if (frame == 0 || frame == 78) {
        std::string order;
        auto& drawOrder = skeleton.getDrawOrder();
        for (size_t i = 0; i < drawOrder.size(); ++i) {
          order += std::to_string(i) + ":" + drawOrder[i]->getData().getName().buffer() + " ";
        }
        printf("ORDER %s\n", order.c_str());
      }
    }
  }
}
