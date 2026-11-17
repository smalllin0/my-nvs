import pytest
from pytest_embedded_idf.dut import IdfDut


@pytest.mark.parametrize('target', ['esp32c3'], indirect=True)
def test_my_nvs(dut: IdfDut) -> None:
    # Unity 每跑完一个用例会打印 <file>:<line>:<case name>:PASS
    for _ in range(11):
        dut.expect(r':PASS', timeout=30)
