//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) 2015 Guillaume Blanc                                         //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//

#define OZZ_INCLUDE_PRIVATE_HEADER  // Allows to include private headers.

#include "animation/offline/fbx/fbx_animation.h"

#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/skeleton_utils.h"
#include "ozz/animation/offline/raw_animation.h"

#include "ozz/base/log.h"

#include "ozz/base/maths/transform.h"

namespace ozz {
namespace animation {
namespace offline {
namespace fbx {

namespace {
bool ExtractAnimation(FbxSceneLoader* _scene_loader,
                      FbxAnimStack* anim_stack,
                      const Skeleton& _skeleton,
                      float _sampling_rate,
                      RawAnimation* _animation) {
  FbxScene* scene = _scene_loader->scene();
  assert(scene);

  // Setup Fbx animation evaluator.
  scene->SetCurrentAnimationStack(anim_stack);

  // Extract animation duration.
  FbxTimeSpan time_spawn;
  const FbxTakeInfo* take_info = scene->GetTakeInfo(anim_stack->GetName());
  if (take_info)
  {
    time_spawn = take_info->mLocalTimeSpan;
  } else {
    scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(time_spawn);
  }

  float start = static_cast<float>(time_spawn.GetStart().GetSecondDouble());
  float end = static_cast<float>(time_spawn.GetStop().GetSecondDouble());

  // Animation duration could be 0 if it's just a pose. In this case we'll set a
  // default 1s duration.
  if (end > start) {
    _animation->duration = end - start;
  } else {
    _animation->duration = 1.f;
  }

  // Allocates all tracks with the same number of joints as the skeleton.
  // Tracks that would not be found will be set to skeleton bind-pose
  // transformation.
  _animation->tracks.resize(_skeleton.num_joints());

  // Iterate all skeleton joints and fills there track with key frames.
  FbxAnimEvaluator* evaluator = scene->GetAnimationEvaluator();
  for (int i = 0; i < _skeleton.num_joints(); i++) {
    RawAnimation::JointTrack& track = _animation->tracks[i];

    // Find a node that matches skeleton joint.
    const char* joint_name = _skeleton.joint_names()[i];
    FbxNode* node = scene->FindNodeByName(joint_name);

    if (!node) {
      // Empty joint track.
      ozz::log::Log() << "No animation track found for joint \"" << joint_name
        << "\". Using skeleton bind pose instead." << std::endl;

      // Get joint's bind pose.
      const ozz::math::Transform& bind_pose =
        ozz::animation::GetJointLocalBindPose(_skeleton, i);

      const RawAnimation::TranslationKey tkey = {0.f, bind_pose.translation};
      track.translations.push_back(tkey);

      const RawAnimation::RotationKey rkey = {0.f, bind_pose.rotation};
      track.rotations.push_back(rkey);

      const RawAnimation::ScaleKey skey = {0.f, bind_pose.scale};
      track.scales.push_back(skey);

      continue;
    }

    // Reserve keys in animation tracks (allocation strategy optimization
    // purpose).
    const float sampling_period = 1.f / _sampling_rate;
    const int max_keys =
      static_cast<int>(3.f + (end - start) / sampling_period);
    track.translations.reserve(max_keys);
    track.rotations.reserve(max_keys);
    track.scales.reserve(max_keys);

    // Evaluate joint transformation at the specified time.
    // Make sure to include "end" time, and enter the loop once at least.
    bool loop_again = true;
    for (float t = start; loop_again; t += sampling_period) {
      if (t >= end) {
        t = end;
        loop_again = false;
      }

      // Evaluate local transform at fbx_time.
      const ozz::math::Transform transform =
        _scene_loader->converter()->ConvertTransform(
          _skeleton.joint_properties()[i].parent == Skeleton::kNoParentIndex?
            evaluator->GetNodeGlobalTransform(node, FbxTimeSeconds(t)):
            evaluator->GetNodeLocalTransform(node, FbxTimeSeconds(t)));

      // Fills corresponding track.
      const float local_time = t - start;
      const RawAnimation::TranslationKey translation = {
        local_time, transform.translation};
      track.translations.push_back(translation);
      const RawAnimation::RotationKey rotation = {
        local_time, transform.rotation};
      track.rotations.push_back(rotation);
      const RawAnimation::ScaleKey scale = {
        local_time, transform.scale};
      track.scales.push_back(scale);
    }
  }

  // Output animation must be valid at that point.
  assert(_animation->Validate());

  return true;
}
}

bool ExtractAnimation(FbxSceneLoader* _scene_loader,
                      const Skeleton& _skeleton,
                      float _sampling_rate,
                      RawAnimation* _animation) {
  FbxScene* scene = _scene_loader->scene();
  assert(scene);

  int anim_stacks_count = scene->GetSrcObjectCount<FbxAnimStack>();

  // Early out if no animation's found.
  if(anim_stacks_count == 0) {
    ozz::log::Err() << "No animation found." << std::endl;
    return false;
  }
  
  if (anim_stacks_count > 1) {
    ozz::log::Log() << anim_stacks_count <<
      " animations found. Only the first one will be exported." << std::endl;
  }

  // Arbitrarily take the first animation of the stack.
  FbxAnimStack* anim_stack = scene->GetSrcObject<FbxAnimStack>(0);
  ozz::log::Log() << "Extracting animation \"" << anim_stack->GetName() << "\""
    << std::endl;
  return ExtractAnimation(_scene_loader,
                          anim_stack,
                          _skeleton,
                          _sampling_rate,
                          _animation);
}
}  // fbx
}  // offline
}  // animation
}  // ozz
