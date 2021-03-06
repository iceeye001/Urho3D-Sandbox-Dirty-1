#pragma once

#include <FlexEngine/Common.h>
#include <FlexEngine/Scene/TriggerAttribute.h>

#include <Urho3D/Scene/Component.h>

namespace FlexEngine
{

/// Dynamic component receives update events and could be automatically updated.
class DynamicComponent : public Component, public EnableTriggers
{
    URHO3D_OBJECT(DynamicComponent, Component);

public:
    /// Construct.
    DynamicComponent(Context* context);
    /// Destruct.
    ~DynamicComponent();
    /// Register object factory.
    static void RegisterObject(Context* context);

    /// Set update period attribute.
    void SetUpdatePeriodAttr(float updatePeriod);
    /// Get update period attribute.
    float GetUpdatePeriodAttr() const;

    /// Mark component as dirty.
    void MarkNeedUpdate();
    /// Is dirty?
    bool DoesNeedUpdate() const;

    /// Update component.
    void Update(bool forceUpdate = true);

private:
    /// Implementation of procedural generator. May be called from a worker thread.
    virtual void DoUpdate() = 0;
    /// Handle force component update.
    void OnUpdate(bool);
    /// Handle update event and update component if needed.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);

protected:
    /// Dirty flag is set if component need update.
    bool dirty_ = false;
    /// Update period.
    float updatePeriod_ = 0.1f;

    /// Accumulated time for update.
    float elapsedTime_ = 0.0f;

};

}
