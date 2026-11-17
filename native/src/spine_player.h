#pragma once

#include <map>
#include <functional>
#include <string>
#include <vector>

#include <spine/spine.h>

#include "pet_state.h"
#include "renderer.h"

struct OperatorManifest {
  std::string id;
  std::string name;
  std::wstring directory;
  std::wstring skelPath[2];
  std::wstring atlasPath[2];
  std::map<std::string, std::string> animations[2];
  std::string rootBone = "F_All";
  float rootClamp = 80.0f;
  float referenceHeight = 520.0f;
  float referenceWidth = 600.0f;
  std::wstring avatarPath;
  bool valid = false;
};

bool loadOperatorManifest(const std::wstring& assetRoot, const std::string& id,
                          OperatorManifest& out);

class SpinePlayer {
 public:
  ~SpinePlayer();

  bool load(Renderer& renderer, const OperatorManifest& manifest, Form initialForm);
  void shutdown();

  void setViewport(int widthPixels, int heightPixels, float dpiScale);
  void play(const PetPlan& plan);
  void update(float deltaSeconds);
  void render(Renderer& renderer);

  void setFacing(int facing);
  bool hitTest(float clientX, float clientY) const;
  void screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const;
  void worldToScreen(float worldX, float worldY, float& screenX, float& screenY) const;
  bool dotScreenPosition(float& screenX, float& screenY) const;

  Form form() const { return form_; }
  void setOnIdle(std::function<void()> callback) { onIdle_ = std::move(callback); }
  void dumpAnimation(const std::string& petKey);
  void setSlotFilter(std::string filter) { slotFilter_ = std::move(filter); }
  void setReverseOrder(bool reverse) { reverseOrder_ = reverse; }

 private:
  struct Anchor {
    float x = 0.0f;
    float y = 0.0f;
  };

  struct Model {
    spine::SkeletonData* data = nullptr;
    spine::Atlas* atlas = nullptr;
    Texture2D* texture = nullptr;
    std::map<std::string, Anchor> anchors;
    std::map<std::string, Anchor> boxesTopRight;
  };

  struct Listener : spine::AnimationStateListenerObject {
    SpinePlayer* player = nullptr;
    void callback(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry,
                  spine::Event* event) override;
  };

  bool loadModel(Renderer& renderer, const OperatorManifest& manifest, int formIndex,
                 Model& out);
  void setModel(Form form);
  void playStep(int index);
  void advance();
  void onComplete(spine::AnimationState* state, spine::TrackEntry* entry);
  Anchor anchorFor(const Step& step) const;
  void updateCamera();

  const OperatorManifest* manifest_ = nullptr;
  Model models_[2];
  Form form_ = Form::Base;
  spine::Skeleton* skeleton_ = nullptr;
  spine::AnimationStateData* stateData_ = nullptr;
  spine::AnimationState* state_ = nullptr;
  spine::TrackEntry* activeEntry_ = nullptr;
  Listener listener_;

  PetPlan plan_;
  int stepIndex_ = 0;
  Anchor anchorBase_;
  Anchor cameraAnchor_;
  Anchor targetAnchor_;
  int facing_ = 1;
  bool ready_ = false;

  int viewportWidth_ = 1;
  int viewportHeight_ = 1;
  float bottomMargin_ = 8.0f;
  float zoom_ = 1.0f;

  float screenLeft_ = 0.0f;
  float screenTop_ = 0.0f;
  float screenRight_ = 0.0f;
  float screenBottom_ = 0.0f;

  std::function<void()> onIdle_;
  std::string slotFilter_;
  bool reverseOrder_ = false;
  std::vector<float> clipPolygon_;
  spine::SlotData* clipEndSlot_ = nullptr;
  std::vector<float> hitTriangles_;
};
