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
