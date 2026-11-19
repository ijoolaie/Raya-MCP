"""Synthetic executable state-machine model; NOT a Witness implementation.

No network, disk persistence, cryptography, consensus, Windows service, or runtime
integration. Inputs named *_verified are trusted test fixtures only.
"""
from dataclasses import dataclass, asdict
from enum import Enum
from copy import deepcopy


class OpState(str, Enum):
    RESERVED = "RESERVED"
    EVIDENCE_COMMITTED = "EVIDENCE_COMMITTED"
    ABORTED = "ABORTED"


class NsState(str, Enum):
    ACTIVE = "ACTIVE"
    RECOVERY_LOCKED = "RECOVERY_LOCKED"
    RETIRED = "RETIRED"


class Result(str, Enum):
    OK = "OK"
    UNKNOWN = "UNKNOWN"
    NOT_FOUND = "NOT_FOUND"
    CONFLICT = "CONFLICT"
    STALE_FENCE = "STALE_FENCE"
    BUSY = "BUSY"
    DENIED = "DENIED"
    RECOVERY_REQUIRED = "RECOVERY_REQUIRED"


@dataclass
class Operation:
    operation_id: str
    sequence: int
    descriptor_digest: str
    reserve_digest: str
    state: OpState
    terminal_digest: str = ""
    authorization_id: str = ""
    authorization_digest: str = ""


class WitnessModel:
    """Single-process serialization model of one namespace's authoritative log."""

    def __init__(self, namespace_id="ns-test", generation=1, fence_epoch=1):
        self.namespace_id = namespace_id
        self.generation = generation
        self.fence_epoch = fence_epoch
        self.sequence = 0
        self.state = NsState.ACTIVE
        self.operations = {}
        self.unresolved_id = None
        self.log_index = 0

    def snapshot(self):
        return deepcopy(self.__dict__)

    @classmethod
    def restore(cls, snapshot):
        obj = cls.__new__(cls)
        obj.__dict__ = deepcopy(snapshot)
        return obj

    def _fence_ok(self, supplied):
        return supplied == self.fence_epoch

    def reserve(self, operation_id, expected_sequence, descriptor_digest,
                request_digest, fence, lose_response=False):
        if not self._fence_ok(fence):
            return Result.STALE_FENCE, None
        if self.state != NsState.ACTIVE:
            return Result.RECOVERY_REQUIRED, None
        existing = self.operations.get(operation_id)
        if existing:
            if existing.reserve_digest != request_digest or existing.descriptor_digest != descriptor_digest:
                return Result.CONFLICT, None
            return Result.OK, deepcopy(existing)
        if self.unresolved_id is not None:
            return Result.BUSY, None
        if expected_sequence != self.sequence:
            return Result.CONFLICT, None
        self.sequence += 1
        self.log_index += 1
        op = Operation(operation_id, self.sequence, descriptor_digest,
                       request_digest, OpState.RESERVED)
        self.operations[operation_id] = op
        self.unresolved_id = operation_id
        # Simulates commit succeeded but the response was lost.
        if lose_response:
            return Result.UNKNOWN, None
        return Result.OK, deepcopy(op)

    def finalize(self, operation_id, sequence, descriptor_digest,
                 terminal_digest, fence, attestations_verified,
                 lose_response=False):
        if not self._fence_ok(fence):
            return Result.STALE_FENCE
        if self.state != NsState.ACTIVE:
            return Result.RECOVERY_REQUIRED
        op = self.operations.get(operation_id)
        if op is None:
            return Result.NOT_FOUND
        if op.sequence != sequence or op.descriptor_digest != descriptor_digest:
            return Result.CONFLICT
        if op.state == OpState.EVIDENCE_COMMITTED:
            return Result.OK if op.terminal_digest == terminal_digest else Result.CONFLICT
        if op.state == OpState.ABORTED:
            return Result.CONFLICT
        if not attestations_verified or not terminal_digest:
            return Result.DENIED
        # Linearization point: this indivisible transition wins over ABORT.
        op.state = OpState.EVIDENCE_COMMITTED
        op.terminal_digest = terminal_digest
        self.unresolved_id = None
        self.log_index += 1
        return Result.UNKNOWN if lose_response else Result.OK

    def abort(self, operation_id, sequence, descriptor_digest,
              terminal_digest, authorization_id, authorization_digest,
              fence, authorization_verified, lose_response=False):
        if not self._fence_ok(fence):
            return Result.STALE_FENCE
        if self.state != NsState.ACTIVE:
            return Result.RECOVERY_REQUIRED
        op = self.operations.get(operation_id)
        if op is None:
            return Result.NOT_FOUND
        if op.sequence != sequence or op.descriptor_digest != descriptor_digest:
            return Result.CONFLICT
        if op.state == OpState.ABORTED:
            same = (op.terminal_digest == terminal_digest and
                    op.authorization_id == authorization_id and
                    op.authorization_digest == authorization_digest)
            return Result.OK if same else Result.CONFLICT
        if op.state == OpState.EVIDENCE_COMMITTED:
            return Result.CONFLICT
        if not authorization_verified or not authorization_id or not authorization_digest or not terminal_digest:
            return Result.DENIED
        # Model assumption: authorization consumption and ABORT share one
        # atomic Witness linearization point. This does NOT prove a real design.
        op.state = OpState.ABORTED
        op.terminal_digest = terminal_digest
        op.authorization_id = authorization_id
        op.authorization_digest = authorization_digest
        self.unresolved_id = None
        self.log_index += 1
        return Result.UNKNOWN if lose_response else Result.OK

    def status(self, operation_id, authoritative=True):
        # A stale/non-linearizable read must never assert NOT_FOUND.
        if not authoritative:
            return Result.UNKNOWN, None
        op = self.operations.get(operation_id)
        if op is None:
            return Result.NOT_FOUND, None
        return Result.OK, deepcopy(op)

    def lock_recovery(self):
        self.state = NsState.RECOVERY_LOCKED

    def advance_fence(self, new_epoch):
        if new_epoch <= self.fence_epoch:
            raise ValueError("fence epoch must strictly increase")
        self.fence_epoch = new_epoch

    def assert_invariants(self):
        assert self.sequence == len(self.operations)
        assert len({op.sequence for op in self.operations.values()}) == len(self.operations)
        terminal = [o for o in self.operations.values()
                    if o.state in (OpState.EVIDENCE_COMMITTED, OpState.ABORTED)]
        assert len([o for o in self.operations.values() if o.state == OpState.RESERVED]) <= 1
        assert (self.unresolved_id is None) == (len([o for o in self.operations.values()
                if o.state == OpState.RESERVED]) == 0)
        if self.unresolved_id is not None:
            assert self.operations[self.unresolved_id].state == OpState.RESERVED
        for op in terminal:
            assert op.terminal_digest
        return True


def dump_state(model):
    return {
        "namespace_id": model.namespace_id,
        "generation": model.generation,
        "fence_epoch": model.fence_epoch,
        "sequence": model.sequence,
        "namespace_state": model.state.value,
        "log_index": model.log_index,
        "operations": {
            key: {**asdict(value), "state": value.state.value}
            for key, value in model.operations.items()
        },
    }
