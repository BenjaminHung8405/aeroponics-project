class RunnerError(Exception): pass
class ParseError(RunnerError): pass
class ConfigError(RunnerError): pass
class StateError(RunnerError): pass
class HumanRequired(RunnerError): pass
