from pytest import fixture

from dataclasses import dataclass
from typing import Optional


@dataclass
class Env:
    prog_path: str
    input_file: Optional[str]
    output_file: Optional[str]


def pytest_addoption(parser):
    parser.addoption("--infile", action="store")
    parser.addoption("--binpath", action="store")


@fixture(scope="session")
def name(request):
    path = request.config.option.binpath or "picprogrammer"
    request.config.getoption("--env")
