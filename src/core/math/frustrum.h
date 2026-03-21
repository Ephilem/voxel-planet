#pragma once

#include <array>

#include "aabb.h"
#include "frustrum.h"


class Frustrum {
    std::array<glm::vec4, 6> m_planes;
public:
    Frustrum() = default;

    void update(const glm::mat4& projectionViewMatrix) {
        // Left plane
        m_planes[0] = glm::vec4(
            projectionViewMatrix[0][3] + projectionViewMatrix[0][0],
            projectionViewMatrix[1][3] + projectionViewMatrix[1][0],
            projectionViewMatrix[2][3] + projectionViewMatrix[2][0],
            projectionViewMatrix[3][3] + projectionViewMatrix[3][0]
        );
        // Right plane
        m_planes[1] = glm::vec4(
            projectionViewMatrix[0][3] - projectionViewMatrix[0][0],
            projectionViewMatrix[1][3] - projectionViewMatrix[1][0],
            projectionViewMatrix[2][3] - projectionViewMatrix[2][0],
            projectionViewMatrix[3][3] - projectionViewMatrix[3][0]
        );
        // Bottom plane
        m_planes[2] = glm::vec4(
            projectionViewMatrix[0][3] + projectionViewMatrix[0][1],
            projectionViewMatrix[1][3] + projectionViewMatrix[1][1],
            projectionViewMatrix[2][3] + projectionViewMatrix[2][1],
            projectionViewMatrix[3][3] + projectionViewMatrix[3][1]
        );
        // Top plane
        m_planes[3] = glm::vec4(
            projectionViewMatrix[0][3] - projectionViewMatrix[0][1],
            projectionViewMatrix[1][3] - projectionViewMatrix[1][1],
            projectionViewMatrix[2][3] - projectionViewMatrix[2][1],
            projectionViewMatrix[3][3] - projectionViewMatrix[3][1]
        );
        // Near plane
        m_planes[4] = glm::vec4(
            projectionViewMatrix[0][3] + projectionViewMatrix[0][2],
            projectionViewMatrix[1][3] + projectionViewMatrix[1][2],
            projectionViewMatrix[2][3] + projectionViewMatrix[2][2],
            projectionViewMatrix[3][3] + projectionViewMatrix[3][2]
        );
        // Far plane
        m_planes[5] = glm::vec4(
            projectionViewMatrix[0][3] - projectionViewMatrix[0][2],
            projectionViewMatrix[1][3] - projectionViewMatrix[1][2],
            projectionViewMatrix[2][3] - projectionViewMatrix[2][2],
            projectionViewMatrix[3][3] - projectionViewMatrix[3][2]
        );

        for (auto& plane : m_planes) {
            float length = glm::length(glm::vec3(plane));
            if (length > 0.0001f) {
                plane /= length;
            }
        }
    }

    bool intersects(const AABB& box) const {
          for (const auto& plane : m_planes) {
              glm::vec3 positive = box.min;
              if (plane.x >= 0) positive.x = box.max.x;
              if (plane.y >= 0) positive.y = box.max.y;
              if (plane.z >= 0) positive.z = box.max.z;

              if (glm::dot(glm::vec3(plane), positive) + plane.w < 0) {
                  return false;
              }
          }
          return true;
      }

      bool contains(const glm::vec3& point) const {
          for (const auto& plane : m_planes) {
              if (glm::dot(glm::vec3(plane), point) + plane.w < 0) {
                  return false;
              }
          }
          return true;
      }
};