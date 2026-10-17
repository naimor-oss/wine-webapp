#!/usr/bin/python3
"""Unit tests of wine-webapp-broker's configuration (no Docker needed).
    python3 tests/broker_test.py        (needs python3-aiohttp)"""
import importlib.machinery
import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
loader = importlib.machinery.SourceFileLoader("broker", os.path.join(HERE, "..", "broker", "wine-webapp-broker"))
spec = importlib.util.spec_from_loader("broker", loader)
broker = importlib.util.module_from_spec(spec)
loader.exec_module(broker)


def write(d, name, text):
    p = os.path.join(d, name)
    with open(p, "w") as f:
        f.write(text)
    return p


class Users(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()

    def test_loads_and_lowercases_identities(self):
        p = write(self.d, "u.toml", '["Alice@EXAMPLE.COM"]\napp_user = "ALICE"\npassword = "x"\nexpect_title = "(Alice Example)"\n')
        users = broker.load_users(p, "app")
        u = users["alice@example.com"]
        self.assertEqual((u.app_user, u.slug, u.expect_title), ("ALICE", "alice", "(Alice Example)"))

    def test_requires_app_user_and_password(self):
        p = write(self.d, "u.toml", '["bob@EXAMPLE.COM"]\napp_user = "BOB"\n')
        with self.assertRaises(broker.ConfigError):
            broker.load_users(p, "app")

    def test_refuses_same_windows_computer_name(self):
        # Wine upper-cases the host name and keeps 15 characters.
        p = write(self.d, "u.toml",
                  '["a@X"]\napp_user = "longusername1"\npassword = "x"\n'
                  '["b@X"]\napp_user = "longusername2"\npassword = "x"\n')
        with self.assertRaisesRegex(broker.ConfigError, "computer name"):
            broker.load_users(p, "app")

    def test_slug(self):
        self.assertEqual(broker.slug_of("J. Smith_2"), "j-smith-2")
        with self.assertRaises(broker.ConfigError):
            broker.slug_of("!!!")


class Idle(unittest.TestCase):
    def cfg(self):
        return broker.load_idle({"default_minutes": 30, "warn_minutes": 10, "window": [
            {"days": "Mon-Fri", "from": "06:00", "to": "18:30", "minutes": 120},
            {"days": "Sat", "from": "08:00", "to": "12:00", "minutes": 60}]})

    def test_days(self):
        self.assertEqual(broker.parse_days("Mon-Fri"), {0, 1, 2, 3, 4})
        self.assertEqual(broker.parse_days("Sat,Sun"), {5, 6})
        self.assertEqual(broker.parse_days("Fri-Mon"), {4, 5, 6, 0})
        with self.assertRaises(broker.ConfigError):
            broker.parse_days("Weekdays")

    def test_limits_by_time_of_day(self):
        from datetime import datetime
        idle = self.cfg()
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 10, 0)), 120)  # Wednesday
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 18, 29)), 120)
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 18, 30)), 30)
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 5, 59)), 30)
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 17, 9, 0)), 60)    # Saturday
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 18, 9, 0)), 30)    # Sunday

    def test_overnight_window(self):
        from datetime import datetime
        idle = broker.load_idle({"default_minutes": 30, "warn_minutes": 5, "window": [
            {"days": "Mon-Sun", "from": "22:00", "to": "02:00", "minutes": 15}]})
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 23, 0)), 15)
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 1, 0)), 15)
        self.assertEqual(broker.idle_limit(idle, datetime(2026, 10, 14, 3, 0)), 30)

    def test_warning_must_be_shorter(self):
        with self.assertRaises(broker.ConfigError):
            broker.load_idle({"default_minutes": 10, "warn_minutes": 10})


    def test_detached(self):
        idle = broker.load_idle({})
        self.assertEqual(idle["detached"], 10)
        s = broker.Session(user=None, container="app-a")
        self.assertFalse(broker.detached_too_long(s, idle, 10_000))   # never had a page
        s.detached_since = 1000.0                                     # connection dropped
        self.assertFalse(broker.detached_too_long(s, idle, 1e9))      # asleep: idle limits
        s.closed_at = 998.0                                           # the page said so
        self.assertFalse(broker.detached_too_long(s, idle, 1000 + 599))
        self.assertTrue(broker.detached_too_long(s, idle, 1000 + 600))
        s.connections = 1                                             # a page is open
        self.assertFalse(broker.detached_too_long(s, idle, 1000 + 6000))
        s.connections = 0
        self.assertFalse(broker.detached_too_long(s, broker.load_idle({"detached_minutes": 0}), 1e9))
        with self.assertRaises(broker.ConfigError):
            broker.load_idle({"detached_minutes": -1})


class WaitingPage(unittest.TestCase):
    def page(self, state):
        b = object.__new__(broker.Broker)
        b.name, b.background = "Some <App>", "#2d3e50"
        s = broker.Session(user=None, container="app-a", state=state, reason="bad password")

        class Request:
            path, headers = "/", {}
        return b.waiting_page(s, Request())

    def test_branded_and_not_cached(self):
        r = self.page("starting")
        self.assertEqual(r.headers["Cache-Control"], "no-store")
        self.assertIn("<title>Some &lt;App&gt;</title>", r.text)
        self.assertIn("background:#2d3e50", r.text)
        self.assertIn('http-equiv="refresh"', r.text)

    def test_failed(self):
        r = self.page("failed")
        self.assertEqual(r.status, 503)
        self.assertIn("bad password", r.text)
        self.assertNotIn("refresh", r.text)


class Config(unittest.TestCase):
    def test_minimal(self):
        d = tempfile.mkdtemp()
        u = write(d, "u.toml", '["a@X"]\napp_user = "A"\npassword = "x"\n')
        c = write(d, "c.toml", f'[broker]\nusers_file = "{u}"\nadmins = ["Admin@X"]\n[session]\nimage = "app:1"\n')
        cfg = broker.load_config(c)
        self.assertEqual(cfg["max_sessions"], 10)
        self.assertEqual(cfg["prefix"], "app")
        self.assertEqual(cfg["admins"], {"admin@x"})
        self.assertIn("a@x", cfg["users"])

    def test_image_required(self):
        d = tempfile.mkdtemp()
        c = write(d, "c.toml", "[session]\n")
        with self.assertRaises(broker.ConfigError):
            broker.load_config(c)

    def test_bad_prefix(self):
        d = tempfile.mkdtemp()
        u = write(d, "u.toml", "")
        c = write(d, "c.toml", f'[broker]\nusers_file = "{u}"\n[session]\nimage = "a"\nname_prefix = "Bad_Prefix"\n')
        with self.assertRaises(broker.ConfigError):
            broker.load_config(c)


if __name__ == "__main__":
    unittest.main(verbosity=1)
