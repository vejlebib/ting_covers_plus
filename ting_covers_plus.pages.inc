<?php
/**
 * @file
 * Retrieves covers for ting objects and returns them in the correct format
 * based on configuration.
 */

// Load ting_covers.pages.inc for its helper functions
module_load_include('inc', 'ting_covers', 'ting_covers.pages');

/**
 * Return the URL for the cover of an object.
 */
function ting_covers_plus_objects() {
  $covers = array();
  $image_styles = array();
  $missing_images_local_ids = array();

  foreach ($_POST['coverData'] as $cover_info) {

    // Determine if the local id is a known negative.
    if (cache_get('ting_covers:' . $cover_info['local_id'], FALSE)) {
      break;
    }

    $path = ting_covers_object_path($cover_info['local_id']);
    if (file_exists($path)) {
      // If we we already have a valid cover image w/o style then just use it.
      $covers[] = array(
        'local_id' => $cover_info['local_id'],
        'image_style' => $cover_info['image_style'],
        'url' => image_style_url($cover_info['image_style'], $path),
      );
    }
    else {
      // Mark the image for retrieval.
      $missing_images_local_ids[] = (object) array(
        'localIdentifier' => $cover_info['local_id'],
        'libraryCode' => $cover_info['owner_id'],
        'materialType' => $cover_info['mtype'],
        'sourceName' => $cover_info['source'],
      );

      // Image style to match later one.
      $image_styles[$cover_info['local_id']] = $cover_info['image_style'];
    }
  }

  // Try to download the missing images.
  try {
    ting_covers_plus_get_image_urls($missing_images_local_ids, $covers, $image_styles);

    // The fallback call for remaining missing images is the ADDI service
    $service = new AdditionalInformationService(variable_get('addi_wsdl_url'), variable_get('addi_username'), variable_get('addi_group'), variable_get('addi_password'));

    // Local ids = Faust numbers. Library object identifiers can be confusing.
    $additional_informations = $service->getByLocalIdentifier($missing_images_local_ids);

    foreach ($missing_images_local_ids as $local_id) {
      // Try to extract the image url from the result.
      $source_url = FALSE;
      if (isset($additional_informations[$local_id->localIdentifier]) && $ai = $additional_informations[$local_id->localIdentifier]) {
        if ($ai->detailUrl) {
          $source_url = $ai->detailUrl;
        }
        elseif ($ai->thumbnailUrl) {
          $source_url = $ai->thumbnailUrl;
        }
      }

      // No cover image found? Pick from standard set of images
      if (!$source_url) {
        if ($source_url = _ting_covers_plus_get_default_image($local_id->materialType, $local_id->sourceName)) {
          // Generate a path corresponding to the default image, styled.
          $covers[] = array(
            'local_id' => $local_id->localIdentifier,
            'image_style' => $image_styles[$cover_info['local_id']],
            'url' => image_style_url($image_styles[$cover_info['local_id']], $source_url),
          );
          continue;
        }
      }

      // NB: The following should now NEVER run, as we always have a source_url defined..
      // No cover image found? Cache this for future reference to avoid
      // unnecessary requests.
      if (!$source_url) {
        cache_set('ting_covers:' . $local_id->localIdentifier, 1, 'cache', $_SERVER['REQUEST_TIME'] + TING_COVERS_DEFAULT_CACHE_LIFETIME);
        continue;
      }

      // Try to download the image locally.
      if ($file = _ting_covers_pages_fetch_image(ting_covers_object_path($local_id->localIdentifier), $source_url)) {
        // Generate a path corresponding to the downloaded image, styled.
        $covers[] = array(
          'local_id' => $local_id->localIdentifier,
          'image_style' => $image_styles[$cover_info['local_id']],
          'url' => image_style_url($image_styles[$cover_info['local_id']], $file),
        );
      }
    }
  }
  catch (Exception $e) {
    watchdog('ting_covers_plus', 'Unable to retrieve covers from ADDI: %message', array('%message' => $e->getMessage()), WATCHDOG_ERROR);
  }

  // Return all image information.
  drupal_json_output($covers);
}


/**
 * Helper function returning default image URI for a given material type
 *
 * @param string $mtype
 *    The material type label to get standard image for.
 * @return string
 *    An URI to an image file.
 */
function _ting_covers_plus_get_default_image($mtype, $source) {
  $source_url = NULL;
  $mtype_uris = variable_get('ting_default_covers_mtype_uris');
  $source_uris = variable_get('ting_default_covers_source_uris');

  // Check first if we have default cover defined for SOURCE NAME
  if (isset($source_uris[$source])) {
    $source_url = $source_uris[$source];
  }
  // Then check if we have default cover defined for MATERIAL TYPE
  elseif (isset($mtype_uris[$mtype])) {
    $source_url = $mtype_uris[$mtype];
  }
  // Last, use the default fallback cover if it has been defined
  elseif (isset($mtype_uris['_default'])) {
    $source_url = $mtype_uris['_default'];
  }

  return $source_url;
}