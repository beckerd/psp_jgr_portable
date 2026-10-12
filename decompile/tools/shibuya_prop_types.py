"""Shibuya types dispatched to the original generic knockable prop task.

030688 assigns 0571e0 directly, or 05725c for the alternate sound bank.
Both constructors install draw callback 0578c0 (small circle 0c3470 and
wedge 0c32cc). Doors, barriers and animated flags use different handlers.
Types 5 and 10 have no placements in the shipped city, but share the task.
"""

SHIBUYA_KNOCKABLE_TYPES = frozenset((4, 5, 10, 12, 13, 14, 15, 16, 17, 18,
                                  19, 20, 21, 22, 27))
