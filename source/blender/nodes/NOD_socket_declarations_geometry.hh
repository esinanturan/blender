/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BKE_geometry_set.hh"

#include "NOD_node_declaration.hh"

namespace blender::nodes::decl {

class GeometryBuilder;

class Geometry : public SocketDeclaration {
 private:
  blender::Vector<bke::GeometryComponent::Type> supported_types_;
  bool only_realized_data_ = false;
  bool only_instances_ = false;

  friend GeometryBuilder;

 public:
  static constexpr eNodeSocketDatatype static_socket_type = SOCK_GEOMETRY;

  using Builder = GeometryBuilder;

  bNodeSocket &build(bNodeTree &ntree, bNode &node) const override;
  bool matches(const bNodeSocket &socket) const override;
  bool can_connect(const bNodeSocket &socket) const override;

  Span<bke::GeometryComponent::Type> supported_types() const;
  bool only_realized_data() const;
  bool only_instances() const;
};

class GeometryBuilder : public SocketDeclarationBuilder<Geometry> {
 public:
  GeometryBuilder &supported_type(bke::GeometryComponent::Type supported_type);
  GeometryBuilder &supported_type(blender::Vector<bke::GeometryComponent::Type> supported_types);
  GeometryBuilder &only_realized_data(bool value = true);
  GeometryBuilder &only_instances(bool value = true);
};

}  // namespace blender::nodes::decl
