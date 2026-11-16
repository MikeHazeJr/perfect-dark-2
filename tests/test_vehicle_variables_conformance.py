"""Maian vehicle V3: the conformance tool's rules for a vehicle's optional
behavior/variables.json (pd.vehicle_variables.v1) match the game's reader
(asset_runtime.c s_hydrateVehicleBoostHeat, [vehicle-boost-heat] in pd-tests)."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_archive_conformance import validate_vehicle_variables  # noqa: E402

GOOD = {
    "schema": "pd.vehicle_variables.v1",
    "boost_speed": 7.0,
    "boost_ramp": {"value": 60, "unit": "ticks60"},
    "overheat_after": {"value": 240, "unit": "ticks60"},
    "overheated_boost": 0.5,
    "steer_limit": 0.5,
    "steer_limit_time": {"value": 45, "unit": "ticks60"},
    "recover_time_stopped": {"value": 120, "unit": "ticks60"},
    "recover_speed_scale": 2.0,
    "cool_crystal_part": 140,
}


def errors_for(variables):
    errors = []
    validate_vehicle_variables("fixture", variables, errors)
    return errors


class VehicleVariablesConformanceTests(unittest.TestCase):
    def test_the_maian_values_pass(self):
        self.assertEqual(errors_for(GOOD), [])

    def test_the_crystal_part_and_other_names_are_optional(self):
        variables = copy.deepcopy(GOOD)
        del variables["cool_crystal_part"]
        variables["turret_regrow"] = {"value": 30, "unit": "ticks60"}
        self.assertEqual(errors_for(variables), [])

    def test_the_mod_file_in_dev_mods_passes(self):
        import json
        path = (Path(__file__).resolve().parents[1] / "dev-mods" /
                "maian_hover_vehicle" / "behavior" / "variables.json")
        self.assertEqual(errors_for(json.loads(path.read_text("utf-8"))), [])

    def test_broken_values_are_refused(self):
        cases = [
            ("schema", "pd.vehicle_variables.v2"),
            ("boost_speed", -1),
            ("boost_ramp", {"value": 0, "unit": "ticks60"}),
            ("boost_ramp", 60),
            ("overheat_after", {"value": 4, "unit": "seconds"}),
            ("overheated_boost", 1.5),
            ("steer_limit", True),
            ("steer_limit_time", {"value": -1, "unit": "ticks60"}),
            ("recover_time_stopped", {"value": 120}),
            ("recover_speed_scale", 0),
            ("cool_crystal_part", -3),
            ("cool_crystal_part", 140.5),
        ]
        for key, value in cases:
            with self.subTest(key=key, value=value):
                variables = copy.deepcopy(GOOD)
                variables[key] = value
                self.assertTrue(errors_for(variables))

    def test_missing_values_are_refused(self):
        for key in ("boost_speed", "boost_ramp", "overheat_after",
                    "overheated_boost", "steer_limit", "steer_limit_time",
                    "recover_time_stopped", "recover_speed_scale"):
            with self.subTest(key=key):
                variables = copy.deepcopy(GOOD)
                del variables[key]
                self.assertTrue(errors_for(variables))

    def test_a_steering_limit_time_of_zero_is_allowed(self):
        variables = copy.deepcopy(GOOD)
        variables["steer_limit_time"] = {"value": 0, "unit": "ticks60"}
        self.assertEqual(errors_for(variables), [])


if __name__ == "__main__":
    unittest.main()
