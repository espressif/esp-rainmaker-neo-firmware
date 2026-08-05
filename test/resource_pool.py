# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Test resource pool implementation.
"""

import threading
from typing import Optional


class ResourceFactory:
    """
    Construct/destruct/reset resources.
    """

    def __init__(self, initializer, resetter=None, deinitializer=None):
        self._initializer = initializer
        self._resetter = resetter
        self._deinitializer = deinitializer

    def get_new_resource(self):
        """
        Get a new instance of the resource.
        """
        return self._initializer()

    def reset_resource(self, resource):
        """
        Reset a resource.
        Must be a resource returned by get_new_resource() earlier.
        """
        if self._resetter:
            try:
                self._resetter(resource)
            except Exception as e:
                print(f"Warning resetting resource: {e}")

    def destroy_resource(self, resource):
        """
        Destroy a resource.
        This will attempt to call the deinitializer, then the resetter.
        Must be a resource returned by get_new_resource() earlier.
        """
        try:
            if self._deinitializer:
                self._deinitializer(resource)
            elif self._resetter:
                self._resetter(resource)
        except Exception as e:
            print(f"Warning deinitializing resource: {e}")


class ResourceContainer:
    """
    Thread-safe container for resources.
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._resources = []

    def is_empty(self) -> bool:
        return len(self._resources) == 0

    def get_resource(self) -> Optional[any]:
        """
        Get a resource from the container.
        Returns None if the container is empty.
        """
        with self._lock:
            if not self.is_empty():
                return self._resources.pop()
            return None

    def add_resource(self, resource: any):
        """
        Add a resource to the container.
        """
        with self._lock:
            self._resources.append(resource)

    def get_all_resources(self) -> list[any]:
        """
        Get all resources from the container into a list and clear the container.
        """
        with self._lock:
            resources = self._resources[:]
            self._resources.clear()
            return resources


class ResourcePool:
    """
    Thread-safe resource pool.
    Provides a lock and ResourceFactory.
    """

    def __init__(self, initializer, resetter=None, deinitializer=None, on_acquire=None):
        self._factory = ResourceFactory(initializer, resetter, deinitializer)
        self._container = ResourceContainer()
        # Optional hook run on every acquire (reused AND fresh-init paths) before
        # the resource is handed to the test.
        self._on_acquire = on_acquire

    def has_reusable_resources(self) -> bool:
        """
        Check if the container has reusable resources.
        """
        return not self._container.is_empty()

    def acquire(self):
        resource = self._container.get_resource()
        if resource is None:
            resource = self._factory.get_new_resource()
        if self._on_acquire:
            try:
                self._on_acquire(resource)
            except Exception as e:
                print(f"Warning in on_acquire hook: {e}")
        return resource

    def release(self, resource):
        self._factory.reset_resource(resource)
        self._container.add_resource(resource)

    def drain_free(self):
        items = []
        try:
            items = self._container.get_all_resources()
        except Exception:
            pass
        for item in items:
            self._factory.destroy_resource(item)
