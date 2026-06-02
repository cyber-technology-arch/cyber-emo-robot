import os
class NoSuchProcess(Exception):
    pass

class Process:
    def __init__(self, pid=None):
        self._pid = pid or os.getpid()
    def parent(self):
        return Process(os.getpid())
    @property
    def pid(self):
        return self._pid
    def create_time(self):
        return 1.0
    def name(self):
        return "cmake"

def process_iter():
    return [Process(os.getpid())]
