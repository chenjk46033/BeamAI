#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace beam::gui {

// The application-level workflow is deliberately independent of Qt and the
// serial device. Views render it; device adapters consult it before issuing
// commands. This prevents individual tabs from inventing their own readiness
// flags, which was the main source of inconsistent state in the initial port.
enum class WorkflowStage : std::size_t {
    CaseSetup = 0,
    SystemCheck,
    Imaging,
    Registration,
    Coupling,
    Correction,
    TreatmentPlan,
    SafetyReview,
    Treatment,
    Report,
    Count
};

enum class WorkflowStatus {
    NotStarted,
    InProgress,
    Complete,
    Invalidated,
    Blocked
};

struct WorkflowStageState {
    WorkflowStatus status = WorkflowStatus::NotStarted;
    std::string message;
};

class TreatmentWorkflow {
public:
    TreatmentWorkflow();

    const WorkflowStageState& state(WorkflowStage stage) const;

    // A stage can begin only after every earlier stage is complete. Returning
    // false leaves the model unchanged and supplies a user-facing explanation.
    bool begin(WorkflowStage stage, std::string* reason = nullptr);
    bool complete(WorkflowStage stage, std::string* reason = nullptr);

    // Records a failed check while keeping downstream work unavailable.
    void block(WorkflowStage stage, std::string message);

    // Call when accepted data at `stage` changes (for example, loading a new
    // MRI). The changed stage becomes InProgress and every downstream stage
    // that had started becomes Invalidated with the supplied explanation.
    void change(WorkflowStage stage, std::string reason);

    bool canBegin(WorkflowStage stage, std::string* reason = nullptr) const;
    bool canStartTreatment(std::string* reason = nullptr) const;
    WorkflowStage nextStage() const;

private:
    static constexpr std::size_t kStageCount = static_cast<std::size_t>(WorkflowStage::Count);
    static constexpr std::size_t index(WorkflowStage stage) { return static_cast<std::size_t>(stage); }
    std::array<WorkflowStageState, kStageCount> stages_{};
};

std::string_view workflowStageName(WorkflowStage stage);
std::string_view workflowStatusName(WorkflowStatus status);

}  // namespace beam::gui
