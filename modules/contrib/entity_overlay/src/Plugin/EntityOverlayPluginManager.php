<?php

namespace Drupal\entity_overlay\Plugin;

use Drupal\Core\Cache\CacheBackendInterface;
use Drupal\Core\Extension\ModuleHandlerInterface;
use Drupal\Core\Plugin\DefaultPluginManager;
use Drupal\entity_overlay\Annotation\EntityOverlayDialog;

/**
 * Provides a entity overlay plugin manager.
 */
class EntityOverlayPluginManager extends DefaultPluginManager {

  /**
   * Constructs a TranscriptionManager object.
   *
   * @param \Traversable $namespaces
   *   An object that implements \Traversable which contains the root paths
   *   keyed by the corresponding namespace to look for plugin implementations.
   * @param \Drupal\Core\Cache\CacheBackendInterface $cache_backend
   *   Cache backend instance to use.
   * @param \Drupal\Core\Extension\ModuleHandlerInterface $module_handler
   *   The module handler to invoke the alter hook with.
   */
  public function __construct(
    \Traversable $namespaces,
    CacheBackendInterface $cache_backend,
    ModuleHandlerInterface $module_handler
  ) {
    parent::__construct(
      'Plugin/EntityOverlay',
      $namespaces,
      $module_handler,
      EntityOverlayDialogInterface::class,
      EntityOverlayDialog::class
    );
    $this->alterInfo('entity_overlay_info');
    $this->setCacheBackend($cache_backend, 'entity_overlay_info_plugins');
  }

}
