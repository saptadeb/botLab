import time


class Rate:
    """!
    @brief      Runs code inside a with block sleeping to maintain the rate
    """

    def __init__(self, rate=None, period=None):
        """!
        @brief      Constructs a new instance.

        @param      rate    The rate in Hz
        @param      period  The period is seconds

        Exactly one of rate or period may be used if both or none are specified an error will be raised
        """
        if (rate is None and period is None) or (rate is not None and period is not None):
            raise Exception('Exactly one of rate or period may be used if both or none are specified an error will be '
                            'raised')

        if rate is not None:
            self.rate = rate
        if period is not None:
            self.period = period
        self._start_time = time.time()

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
        self._start_time = time.time()
        return self

    def __exit__(self, type, value, traceback):
        elapsed = time.time() - self._start_time
        if elapsed >= self._period:
            # Enough time has already elapsed
            return
        # Sleep the remaining time
        time.sleep(self._period - elapsed)

