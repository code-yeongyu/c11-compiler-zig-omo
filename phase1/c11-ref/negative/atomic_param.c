void takes_atomic(_Atomic int value)
{
    (void)value;
}

#error atomic parameter constraint expected failure: _Atomic-qualified parameters are rejected by this negative suite
