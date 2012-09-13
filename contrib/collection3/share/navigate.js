function nav_init (time_begin, time_end)
{
  var all_images;
  var i;

  all_images = document.getElementsByTagName ("img");
  for (i = 0; i < all_images.length; i++)
  {
    if (all_images[i].className != "graph_image")
      continue;

    all_images[i].navTimeBegin = new Number (time_begin);
    all_images[i].navTimeEnd   = new Number (time_end);

    all_images[i].navBaseURL = all_images[i].src.replace (/;(begin|end)=[^;]*/g, '');

    if (all_images[i].addEventListener) /* Mozilla */
    {
      all_images[i].addEventListener ('dblclick', nav_handle_dblclick,
          false /* == bubbling */);
      all_images[i].addEventListener ('DOMMouseScroll', nav_handle_wheel,
          false /* == bubbling */);
    }
    else
    {
      all_images[i].ondblclick = nav_handle_dblclick;
      all_images[i].onmousewheel = nav_handle_wheel;
    }
  }

  return (true);
} /* nav_init */

function nav_image_repaint (img)
{
  if (!img || !img.navBaseURL
      || !img.navTimeBegin || !img.navTimeEnd)
    return;

  img.src = img.navBaseURL + ";"
    + "begin=" + img.navTimeBegin.toFixed (0) + ";"
    + "end=" + img.navTimeEnd.toFixed (0);
} /* nav_image_repaint */

function nav_time_reset (img_id ,diff)
{
  var img;

  img = document.getElementById (img_id);
  if (!img)
    return (false);

  img.navTimeEnd = new Number ((new Date ()).getTime () / 1000);
  img.navTimeBegin = new Number (img.navTimeEnd - diff);

  nav_image_repaint (img);

  return (true);
}

function nav_time_change_obj (img, factor_begin, factor_end)
{
  var diff;

  if (!img || !img.navBaseURL
      || !img.navTimeBegin || !img.navTimeEnd)
    return (false);

  diff = img.navTimeEnd - img.navTimeBegin;

  /* Prevent zooming in if diff is less than five minutes */
  if ((diff <= 300) && (factor_begin > 0.0) && (factor_end < 0.0))
    return (true);

  img.navTimeBegin += (diff * factor_begin);
  img.navTimeEnd   += (diff * factor_end);

  nav_image_repaint (img);

  return (true);
} /* nav_time_change */

function nav_time_change (img_id, factor_begin, factor_end)
{
  var diff;

  if (img_id == '*')
  {
    var all_images;
    var i;

    all_images = document.getElementsByTagName ("img");
    for (i = 0; i < all_images.length; i++)
    {
      if (all_images[i].className != "graph_image")
        continue;
    
      nav_time_change_obj (all_images[i], factor_begin, factor_end);
    }
  }
  else
  {
    var img;

    img = document.getElementById (img_id);
    if (!img)
      return (false);

    nav_time_change_obj (img, factor_begin, factor_end);
  }

  return (true);
} /* nav_time_change */

function nav_move_earlier (img_id)
{
  return (nav_time_change (img_id, -0.2, -0.2));
} /* nav_move_earlier */

function nav_move_later (img_id)
{
  return (nav_time_change (img_id, +0.2, +0.2));
} /* nav_move_later */

function nav_zoom_in (img_id)
{
  return (nav_time_change (img_id, +0.2, -0.2));
} /* nav_zoom_in */

function nav_zoom_out (img_id)
{
  return (nav_time_change (img_id, (-1.0 / 3.0), (1.0 / 3.0)));
} /* nav_zoom_in */

function nav_set_reference (img_id)
{
  var img;
  var all_images;
  var tmp;
  var i;

  img = document.getElementById (img_id);
  if (!img || (img.className != "graph_image")
      || !img.navTimeBegin || !img.navTimeEnd)
    return;

  all_images = document.getElementsByTagName ("img");
  for (i = 0; i < all_images.length; i++)
  {
    tmp = all_images[i];
    if (!tmp || (tmp.className != "graph_image")
        || !tmp.navTimeBegin || !tmp.navTimeEnd)
      continue;

    if (tmp.id == img_id)
      continue;

    tmp.navTimeBegin = img.navTimeBegin;
    tmp.navTimeEnd = img.navTimeEnd;

    nav_image_repaint (tmp);
  }
} /* nav_set_reference */

/* 
 * TODO: calculate the mouse position relative to the image in a cross-browser
 * manner.
 */
function nav_calculate_offset_x (obj)
{
  var offset = 0;

  if (!obj)
    return (offset);

  offset = obj.offsetLeft;
  if (obj.offsetParent)
    offset += nav_calculate_offset_x (obj.offsetParent);

  return (offset);
} /* nav_calculate_offset_x */

function nav_calculate_event_x (e)
{
  var pos = 0;
  var off = 0;

  if (!e || !e.target)
    return;
  
  off = nav_calculate_offset_x (e.target);

  if (e.pageX || e.pageY)
  {
    pos = e.pageX;
  }
  else if (e.clientX || e.clientY)
  {
    pos = e.clientX + document.body.scrollLeft
      + document.documentElement.scrollLeft;
  }

  return (pos);
} /* nav_calculate_event_x */

function nav_recenter (e)
{
  var x;
  var y;
  var img;
  var diff;
  var time_old_center;
  var time_new_center;
  var width;

  img = e.target;
  if (!img || (img.className != "graph_image")
      || !img.navTimeBegin || !img.navTimeEnd)
    return;

  width = img.width - 97;

  x = e.layerX - 70;
  if (!x || (x < 0) || (x > width))
    return;

  y = e.layerY;
  if (!y || (y < 35) || (y > 135))
    return;

  diff = img.navTimeEnd - img.navTimeBegin;

  time_old_center = img.navTimeBegin + (diff / 2.0);
  time_new_center = img.navTimeBegin + (x * diff / width);

  img.navTimeBegin += (time_new_center - time_old_center);
  img.navTimeEnd   += (time_new_center - time_old_center);
} /* nav_recenter */

function nav_handle_dblclick (e)
{
  var img;

  /* M$IE */
  if (!e)
    e = window.event;

  img = e.target;
  if (!img || (img.className != "graph_image")
      || !img.navTimeBegin || !img.navTimeEnd)
    return;

  nav_recenter (e);
  nav_image_repaint (img);

  // e.returnValue = false;
} /* nav_handle_dblclick */

/* Taken from <http://adomas.org/javascript-mouse-wheel/> */
function nav_handle_wheel (e)
{
  var delta = 0;
  var img;
  
  /* M$IE */
  if (!e)
    e = window.event;

  img = e.target;
  if (!img || (img.className != "graph_image")
      || !img.navTimeBegin || !img.navTimeEnd)
    return;

  /* Opera and M$IE */
  if (e.wheelDelta)
  {
    delta = e.wheelDelta; 
    if (window.opera)
      delta = delta * (-1);
  }
  else if (e.detail)
  {
    delta = e.detail * (-1);
  }

  if (!delta)
    return;

  nav_recenter (e);
  if (delta > 0)
    nav_zoom_in (img.id);
  else
    nav_zoom_out (img.id);

  if (e.preventDefault)
    e.preventDefault ();
  e.returnValue = false;
} /* function nav_handle_wheel */

/* vim: set sw=2 sts=2 et : */
