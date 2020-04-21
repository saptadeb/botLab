import time


class Rate:
    """!
    @brief      Runs code inside a with block sleeping to maintain the rate
    """

    def __init__(self, rate=None, period=None, quiet=True):
        """!
        @brief      Constructs a new instance.

        @param      rate    The rate in Hz
        @param      period  The period is seconds
        @param      quiet   If false then an exception is raised when the Rate cannot be maintained

        Exactly one of rate or period may be used if both or none are specified an error will be raised
        """
        if (rate is None and period is None) or (rate is not None and period is not None):
            raise Exception('Exactly one of rate or period may be used if both or none are specified an error will be '
                            'raised')

        if rate is not None:
            self.rate = rate
        if period is not None:
            self.period = period
        self._quiet = quiet
        self._start_time = time.perf_counter()

    @property
    def rate(self):
        return self._rate

    @rate.setter
    def rate(self, rate):
        self._rate = rate
        self._period = 1 / self._rate

    @property
    def period(self):
        return self._period

    @period.setter
    def period(self, period):
        self._period = period
        self._rate = 1 / self._period

    def __enter__(self):
        self._start_time = time.perf_counter()
        return self

    def __exit__(self, type, value, traceback):
        elapsed = time.perf_counter() - self._start_time
        if elapsed >= self._period:
            if not self._quiet:
                raise Exception('Rate of {} fell behind by {}s'.format(self._rate, elapsed - self._period))
            # Enough time has already elapsed
            return
        # Sleep the remaining time
        sleep_start = time.perf_counter()
        time_left = self._period - elapsed
        while time_left > 5e-3:
            time.sleep(time_left / 2)
            time_left = self._period - (time.perf_counter() - self._start_time)
        sleep_time = time.perf_counter() - sleep_start

