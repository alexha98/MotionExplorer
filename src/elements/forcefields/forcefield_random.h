#pragma once
#include "forcefield.h"

class UniformRandomForceField: public ForceField{
  public:
    UniformRandomForceField(Math3D::Vector3 _minforce, Math3D::Vector3 _maxforce);

    virtual Math3D::Vector3 getForce(const Math3D::Vector3& position, const Math3D::Vector3& velocity) override;
    virtual void Print(std::ostream &out) const override;
    virtual ForceFieldTypes type() override;

  private:
    Math3D::Vector3 minforce, maxforce;
};

class GaussianRandomForceField: public ForceField{
  public:
    GaussianRandomForceField(Math3D::Vector3 _mean, Math3D::Vector3 _std);

    virtual Math3D::Vector3 getForce(const Math3D::Vector3& position, const Math3D::Vector3& velocity) override;
    virtual void Print(std::ostream &out) const override;
    virtual ForceFieldTypes type() override;

  private:
    Math3D::Vector3 mean, stddeviation;
};

