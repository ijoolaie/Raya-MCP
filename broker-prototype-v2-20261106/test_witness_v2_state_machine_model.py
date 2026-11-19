import unittest
from witness_v2_state_machine_model import WitnessModel, OpState, NsState, Result


class WitnessModelTests(unittest.TestCase):
    def setUp(self):
        self.w = WitnessModel()
        self.args = dict(operation_id="op-1", expected_sequence=0,
                         descriptor_digest="desc-a", request_digest="reserve-a", fence=1)
        result, self.op = self.w.reserve(**self.args)
        self.assertEqual(result, Result.OK)

    def finalize(self, **overrides):
        args = dict(operation_id="op-1", sequence=1, descriptor_digest="desc-a",
                    terminal_digest="final-a", fence=1, attestations_verified=True)
        args.update(overrides)
        return self.w.finalize(**args)

    def abort(self, **overrides):
        args = dict(operation_id="op-1", sequence=1, descriptor_digest="desc-a",
                    terminal_digest="abort-a", authorization_id="auth-1",
                    authorization_digest="auth-digest-1", fence=1,
                    authorization_verified=True)
        args.update(overrides)
        return self.w.abort(**args)

    def test_reserve_consumes_sequence_once_and_is_idempotent(self):
        result, first = self.w.reserve(**self.args)
        self.assertEqual(result, Result.OK)
        self.assertEqual(first.sequence, 1)
        self.assertEqual(self.w.sequence, 1)
        self.assertEqual(self.w.reserve(**self.args)[0], Result.OK)
        self.assertEqual(self.w.sequence, 1)
        self.assertTrue(self.w.assert_invariants())

    def test_same_id_different_digest_conflicts(self):
        changed = dict(self.args, request_digest="different")
        self.assertEqual(self.w.reserve(**changed)[0], Result.CONFLICT)
        self.assertEqual(self.w.sequence, 1)

    def test_one_unresolved_reservation_blocks_next(self):
        args = dict(operation_id="op-2", expected_sequence=1,
                    descriptor_digest="desc-b", request_digest="reserve-b", fence=1)
        self.assertEqual(self.w.reserve(**args)[0], Result.BUSY)
        self.assertEqual(self.w.sequence, 1)

    def test_finalize_terminal_and_idempotent(self):
        self.assertEqual(self.finalize(), Result.OK)
        self.assertEqual(self.finalize(), Result.OK)
        self.assertEqual(self.w.operations["op-1"].state, OpState.EVIDENCE_COMMITTED)
        self.assertTrue(self.w.assert_invariants())

    def test_finalize_denied_without_verified_attestations(self):
        self.assertEqual(self.finalize(attestations_verified=False), Result.DENIED)
        self.assertEqual(self.w.operations["op-1"].state, OpState.RESERVED)

    def test_abort_terminal_and_idempotent(self):
        self.assertEqual(self.abort(), Result.OK)
        self.assertEqual(self.abort(), Result.OK)
        op = self.w.operations["op-1"]
        self.assertEqual(op.state, OpState.ABORTED)
        self.assertEqual(op.authorization_id, "auth-1")
        self.assertEqual(self.w.sequence, 1)
        self.assertTrue(self.w.assert_invariants())

    def test_abort_denied_without_authorization(self):
        self.assertEqual(self.abort(authorization_verified=False), Result.DENIED)
        self.assertEqual(self.w.operations["op-1"].state, OpState.RESERVED)

    def test_finalize_wins_then_abort_conflicts(self):
        self.assertEqual(self.finalize(), Result.OK)
        self.assertEqual(self.abort(), Result.CONFLICT)
        self.assertEqual(self.w.operations["op-1"].state, OpState.EVIDENCE_COMMITTED)

    def test_abort_wins_then_finalize_conflicts(self):
        self.assertEqual(self.abort(), Result.OK)
        self.assertEqual(self.finalize(), Result.CONFLICT)
        self.assertEqual(self.w.operations["op-1"].state, OpState.ABORTED)

    def test_stale_fence_rejected_at_mutation(self):
        self.w.advance_fence(2)
        self.assertEqual(self.finalize(), Result.STALE_FENCE)
        self.assertEqual(self.w.operations["op-1"].state, OpState.RESERVED)

    def test_non_authoritative_status_never_says_not_found(self):
        result, op = self.w.status("missing", authoritative=False)
        self.assertEqual(result, Result.UNKNOWN)
        self.assertIsNone(op)

    def test_authoritative_status_not_found(self):
        self.assertEqual(self.w.status("missing", authoritative=True)[0], Result.NOT_FOUND)

    def test_lost_reserve_response_recovers_by_status(self):
        w = WitnessModel()
        args = dict(operation_id="op-x", expected_sequence=0,
                    descriptor_digest="desc-x", request_digest="reserve-x", fence=1)
        self.assertEqual(w.reserve(**args, lose_response=True)[0], Result.UNKNOWN)
        result, op = w.status("op-x")
        self.assertEqual(result, Result.OK)
        self.assertEqual(op.state, OpState.RESERVED)
        self.assertEqual(w.sequence, 1)

    def test_lost_finalize_response_recovers_by_status(self):
        self.assertEqual(self.finalize(lose_response=True), Result.UNKNOWN)
        result, op = self.w.status("op-1")
        self.assertEqual(result, Result.OK)
        self.assertEqual(op.state, OpState.EVIDENCE_COMMITTED)

    def test_lost_abort_response_recovers_by_status(self):
        self.assertEqual(self.abort(lose_response=True), Result.UNKNOWN)
        result, op = self.w.status("op-1")
        self.assertEqual(result, Result.OK)
        self.assertEqual(op.state, OpState.ABORTED)

    def test_snapshot_restore_preserves_committed_terminal_state(self):
        self.assertEqual(self.finalize(), Result.OK)
        restored = WitnessModel.restore(self.w.snapshot())
        self.assertEqual(restored.status("op-1")[1].state, OpState.EVIDENCE_COMMITTED)
        self.assertEqual(restored.sequence, 1)
        self.assertTrue(restored.assert_invariants())

    def test_recovery_lock_blocks_mutations(self):
        self.w.lock_recovery()
        args = dict(operation_id="op-2", expected_sequence=1,
                    descriptor_digest="desc-b", request_digest="reserve-b", fence=1)
        self.assertEqual(self.w.reserve(**args)[0], Result.RECOVERY_REQUIRED)
        self.assertEqual(self.finalize(), Result.RECOVERY_REQUIRED)

    def test_sequence_never_reused_after_abort(self):
        self.assertEqual(self.abort(), Result.OK)
        args = dict(operation_id="op-2", expected_sequence=0,
                    descriptor_digest="desc-b", request_digest="reserve-b", fence=1)
        self.assertEqual(self.w.reserve(**args)[0], Result.CONFLICT)
        self.assertEqual(self.w.sequence, 1)

    def test_terminal_digest_mismatch_conflicts(self):
        self.assertEqual(self.finalize(), Result.OK)
        self.assertEqual(self.finalize(terminal_digest="different"), Result.CONFLICT)

    def test_all_serialized_finalize_abort_orders_have_one_winner(self):
        for first, second, expected in (
            ("finalize", "abort", OpState.EVIDENCE_COMMITTED),
            ("abort", "finalize", OpState.ABORTED),
        ):
            w = WitnessModel()
            w.reserve(**self.args)
            if first == "finalize":
                self.assertEqual(w.finalize("op-1", 1, "desc-a", "final-a", 1, True), Result.OK)
                self.assertEqual(w.abort("op-1", 1, "desc-a", "abort-a", "auth-1",
                                         "auth-digest-1", 1, True), Result.CONFLICT)
            else:
                self.assertEqual(w.abort("op-1", 1, "desc-a", "abort-a", "auth-1",
                                         "auth-digest-1", 1, True), Result.OK)
                self.assertEqual(w.finalize("op-1", 1, "desc-a", "final-a", 1, True), Result.CONFLICT)
            self.assertEqual(w.operations["op-1"].state, expected)
            self.assertTrue(w.assert_invariants())


if __name__ == "__main__":
    unittest.main(verbosity=2)
